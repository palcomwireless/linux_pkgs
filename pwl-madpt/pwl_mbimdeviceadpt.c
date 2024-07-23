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

#include <stdbool.h>
#include <stdio.h>

#include "common.h"
#include "pwl_mbimdeviceadpt.h"

pthread_mutex_t g_device_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_device_cond = PTHREAD_COND_INITIALIZER;

static MbimDevice *g_device;
static GCancellable *g_cancellable;
mbim_device_ready_callback g_ready_cb;
static gboolean g_device_opened = FALSE;


static void at_command_query_cb(MbimDevice *dev, GAsyncResult *res, gpointer user_data) {
    g_autoptr(MbimMessage) response = NULL;
    g_autoptr(GError) error = NULL;
    guint32 command_resp_size;
    const guint8 *command_resp;
    mbim_at_resp_callback cb;

    cb = user_data;

    response = mbim_device_command_finish(dev, res, &error);
    if (response &&
        mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_compal_at_command_response_parse(response, &command_resp_size,
                                                      &command_resp, &error)) {

        if (DEBUG) PWL_LOG_DEBUG("MBIM Response: %s\n", command_resp);
        if (cb) cb((unsigned char *)command_resp);

    } else {
        PWL_LOG_ERR("Couldn't query at command services, error: %s", error->message);
        guint8 *error_resp = "QUERY ERROR";
        if (cb) cb(error_resp);
    }
}

void pwl_mbimdeviceadpt_at_req(char *command, mbim_at_resp_callback cb) {

    if (DEBUG) PWL_LOG_DEBUG("cmd: %s", command);

    g_autoptr(MbimMessage) message = NULL;

    size_t command_req_size = strlen(command) + strlen("\r\n") + 1;
    guint8 *command_req = (guint8 *) malloc(command_req_size);
    memset(command_req, 0, command_req_size);
    sprintf(command_req, "%s%s", command, "\r\n");

    message = mbim_message_compal_at_command_query_new(command_req_size,
              (const guint8 *)command_req, NULL);
    mbim_device_command(g_device, message, (PWL_CMD_TIMEOUT_SEC - 1), NULL,
                        (GAsyncReadyCallback)at_command_query_cb, (gpointer)cb);

    if (command_req) {
        free(command_req);
    }

}

static void mbim_device_close_cb(MbimDevice *dev, GAsyncResult *res) {
    PWL_LOG_INFO("MBIM Device close cb");
    GError *error = NULL;

    if (!mbim_device_close_finish(dev, res, &error))
        g_error_free(error);

    pthread_cond_signal(&g_device_cond);
}

static void device_close() {
    PWL_LOG_INFO("MBIM Device close");

    mbim_device_close(g_device, PWL_CLOSE_MBIM_TIMEOUT_SEC, NULL,
                     (GAsyncReadyCallback) mbim_device_close_cb, NULL);

    g_clear_object(&g_device);
}

static void device_open_cb(MbimDevice *dev, GAsyncResult *res) {
    PWL_LOG_INFO("MBIM Device open");
    g_autoptr(GError) error = NULL;

    if (!mbim_device_open_finish(dev, res, &error)) {
        PWL_LOG_ERR("Couldn't open Mbim Device: %s\n", error->message);
        g_device_opened = FALSE;
    } else {
        PWL_LOG_DEBUG("MBIM Device %s opened.", mbim_device_get_path_display(dev));
        g_device_opened = TRUE;
    }

    if (g_ready_cb != NULL) {
        g_ready_cb(g_device_opened);
    }
}

static void device_new_cb(GObject *unused, GAsyncResult *res) {
    PWL_LOG_INFO("MBIM Device new cb");

    g_autoptr(GError) error = NULL;

    g_device = mbim_device_new_finish(res, &error);
    if (!g_device) {
        PWL_LOG_ERR("Couldn't create MbimDevice object: %s\n", error->message);
        return;
    }

    mbim_device_open_full(g_device, MBIM_DEVICE_OPEN_FLAGS_PROXY,
                         (PWL_CMD_TIMEOUT_SEC - 1), g_cancellable,
                         (GAsyncReadyCallback) device_open_cb, NULL);
}

gboolean pwl_mbimdeviceadpt_init(mbim_device_ready_callback cb) {
    PWL_LOG_INFO("MBIM Device init");

    g_autoptr(GFile) file = NULL;

    gchar port[20];
    memset(port, 0, sizeof(port));
    if (!pwl_find_mbim_port(port, sizeof(port))) {
        PWL_LOG_ERR("find mbim port fail at mbim device adpt init!");
        return FALSE;
    }

    g_device_opened = FALSE;

    file = g_file_new_for_path(port);
    g_cancellable = g_cancellable_new();

    mbim_device_new(file, g_cancellable, (GAsyncReadyCallback) device_new_cb, NULL);

    g_ready_cb = cb;

    return TRUE;
}

void pwl_mbimdeviceadpt_deinit() {
    PWL_LOG_INFO("MBIM Device deinit");

    device_close();
    if (!cond_wait(&g_device_mutex, &g_device_cond, PWL_CLOSE_MBIM_TIMEOUT_SEC)) {
        if (DEBUG) PWL_LOG_ERR("timed out or error during mbim deinit");
    }

    if (g_cancellable)
        g_object_unref(g_cancellable);
    if (g_device)
        g_object_unref(g_device);

    g_cancellable = NULL;
    g_device = NULL;
    g_ready_cb = NULL;
}

gboolean pwl_mbimdeviceadpt_port_wait() {
    for (int i = 0; i < 10; i++) {
        gchar port[20];
        memset(port, 0, sizeof(port));
        if (pwl_find_mbim_port(port, sizeof(port))) {
            return TRUE;
        }
        sleep(5);
    }
    return FALSE;
}
