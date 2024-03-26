/*
 * Copyright (C) 2024 Palcom International Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses>.
 */

#include <stdio.h>
#include <sys/msg.h>

#include "common.h"
#include "dbus_common.h"
#include "log.h"
#include "pwl_core.h"

static GMainLoop *gp_loop;
static pwlCore *gp_skeleton = NULL;

void send_message_queue(uint32_t cid) {
    mqd_t mq;
    mq = mq_open(CID_DESTINATION(cid), O_WRONLY);

    // message to be sent
    msg_buffer_t message;
    message.pwl_cid = cid;
    message.status = PWL_CID_STATUS_NONE;
    message.sender_id = PWL_MQ_ID_CORE;

    // msgsnd to send message
    mq_send(mq, (gchar *)&message, sizeof(message), 0);
}

static gboolean emit_get_fw_version(gconstpointer p) {
    PWL_LOG_DEBUG("emit_get_fw_version() is called.");
    pwl_core_emit_get_fw_version_signal(gp_skeleton);

    return FALSE;
}

static gboolean madpt_ready_method(pwlCore     *object,
                           GDBusMethodInvocation *invocation) {

    PWL_LOG_DEBUG("Madpt ready, send signal to get FW version!");
    pwl_core_emit_get_fw_version_signal(gp_skeleton);

    return TRUE;

}

static gboolean ready_to_fcc_unlock_method(pwlCore     *object,
                           GDBusMethodInvocation *invocation) {

    PWL_LOG_DEBUG("FW update done, ready to fcc unlock");

    return TRUE;

}

static void bus_acquired_hdl(GDBusConnection *connection,
                             const gchar     *bus_name,
                             gpointer         user_data) {
    GError *pError = NULL;

    /** Second step: Try to get a connection to the given bus. */
    gp_skeleton = pwl_core_skeleton_new();

    /** Third step: Attach to dbus signals. */
    (void) g_signal_connect(gp_skeleton, "handle-madpt-ready-method", G_CALLBACK(madpt_ready_method), NULL);
    (void) g_signal_connect(gp_skeleton, "handle-ready-to-fcc-unlock-method", G_CALLBACK(madpt_ready_method), NULL);

    /** Fourth step: Export interface skeleton. */
    (void) g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(gp_skeleton),
                                            connection,
                                            PWL_GDBUS_OBJ_PATH,
                                            &pError);
    if(pError != NULL) {
        PWL_LOG_ERR("Failed to export object. Reason: %s.", pError->message);
        g_error_free(pError);
        g_main_loop_quit(gp_loop);
        return;
    }
}

static void name_acquired_hdl(GDBusConnection *connection,
                              const gchar     *bus_name,
                              gpointer         user_data) {
    PWL_LOG_INFO("Acquired bus name: %s", PWL_GDBUS_NAME);
}

static void name_lost_hdl(GDBusConnection *connection,
                          const gchar     *bus_name,
                          gpointer         user_data) {
    if(connection == NULL) {
        PWL_LOG_ERR("Failed to connect to dbus");
    } else {
        PWL_LOG_ERR("Failed to obtain bus name %s", PWL_GDBUS_NAME);
    }

    g_main_loop_quit(gp_loop);
}

gint main() {
    PWL_LOG_INFO("start");

    gint owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
                                   PWL_GDBUS_NAME,
                                   G_BUS_NAME_OWNER_FLAGS_NONE,
                                   bus_acquired_hdl,
                                   name_acquired_hdl,
                                   name_lost_hdl,
                                   NULL,
                                   NULL);

    if (owner_id < 0) {
        PWL_LOG_ERR("bus init failed!");
        return 0;
    }

    gp_loop = g_main_loop_new(NULL, FALSE);

    g_main_loop_run(gp_loop);

    // de-init
    if (0 != gp_loop) {
        g_main_loop_quit(gp_loop);
        g_main_loop_unref(gp_loop);
    }

    return 0;
}
