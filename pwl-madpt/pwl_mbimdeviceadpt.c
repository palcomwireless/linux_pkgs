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

#if defined(AT_OVER_MBIM_API)
pthread_mutex_t g_device_mutex = PTHREAD_MUTEX_INITIALIZER;

static gboolean device_open_proxy_flag = TRUE;
static gboolean device_open_ms_mbimex_v2_flag = FALSE;
static gboolean device_open_ms_mbimex_v3_flag = FALSE;
static MbimDevice *device;
static GCancellable *cancellable;
mbim_device_ready_callback g_ready_cb;


#if defined(AT_OVER_MBIM_API)
static void
mbim_query_device_services_ready (MbimDevice   *device,
                                  GAsyncResult *res,
                                  GTask        *task)
{
    MbimMessage               *response;
    GError                    *error = NULL;
    MbimDeviceServiceElement **device_services;
    guint32                    device_services_count;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_device_services_response_parse (
            response,
            &device_services_count,
            NULL, /* max_dss_sessions */
            &device_services,
            &error)) {
        PWL_LOG_INFO("Total of %d device services.", device_services_count);
        guint32 i;

        /* Look for the QMI service */
        for (i = 0; i < device_services_count; i++) {
            if (mbim_uuid_to_service (&device_services[i]->device_service_id) == MBIM_SERVICE_COMPAL) {
                if (DEBUG) PWL_LOG_DEBUG("COMPAL(ID%d) device service found",
                                         mbim_uuid_to_service (&device_services[i]->device_service_id));
                break;
            }
        }
        mbim_device_service_element_array_free (device_services);
    } else {
        /* Ignore error */
        PWL_LOG_ERR("Couldn't query device services, error: %s", error->message);
        g_error_free (error);
    }

    if (response)
        mbim_message_unref (response);
}

static void
mbim_query_device_services ()
{
    MbimMessage *message;

    message = mbim_message_device_services_query_new (NULL);
    mbim_device_command (device,
                         message,
                         (PWL_CMD_TIMEOUT_SEC - 1),
                         NULL,
                         (GAsyncReadyCallback)mbim_query_device_services_ready,
                         NULL);
    mbim_message_unref (message);
}
#endif /* AT_OVER_MBIM_API */

static void
at_command_query_cb (MbimDevice *device,
                     GAsyncResult *res,
                     gpointer user_data)
{
    MbimMessage               *response;
    GError                    *error = NULL;
    guint32                    command_resp_size;
    const guint8              *command_resp;
    mbim_at_resp_callback      cb;

    cb = user_data;

    response = mbim_device_command_finish (device, res, &error);
    if (response &&
        mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error) &&
        mbim_message_compal_at_command_response_parse (
            response,
            &command_resp_size,
            &command_resp,
            &error)) {

        if (DEBUG) PWL_LOG_DEBUG("Response: %s\n", command_resp);
        if (cb) cb(command_resp);

    } else {
        PWL_LOG_ERR("Couldn't query at command services, error: %s", error->message);
        guint8 *error_resp = "ERROR";
        if (cb) cb(error_resp);
        g_error_free (error);
    }

    if (response)
        mbim_message_unref (response);
}

void pwl_mbimdeviceadpt_at_req(char *command, mbim_at_resp_callback cb) {
    pthread_mutex_lock(&g_device_mutex);

    if (DEBUG) PWL_LOG_DEBUG("cmd: %s", command);

    MbimMessage *message;

    size_t command_req_size = strlen(command) + strlen("\r\n") + 1;
    char *command_req = (char *) malloc(command_req_size);
    memset(command_req, 0, command_req_size);
    sprintf(command_req, "%s%s", command, "\r\n");

    message = mbim_message_compal_at_command_query_new (command_req_size, (const guint8 *)command_req, NULL);
    mbim_device_command (device,
                         message,
                         (PWL_CMD_TIMEOUT_SEC - 1),
                         NULL,
                         (GAsyncReadyCallback)at_command_query_cb,
                         (gpointer)cb);
    mbim_message_unref (message);

    if (command_req) {
        free(command_req);
    }

    pthread_mutex_unlock(&g_device_mutex);
}

static void
mbim_device_close_ready (MbimDevice   *dev,
                         GAsyncResult *res)
{
    PWL_LOG_INFO("MBIM Device close ready");
    GError *error = NULL;

    if (!mbim_device_close_finish (dev, res, &error))
        g_error_free (error);

    pthread_mutex_unlock(&g_device_mutex);
}

static void
device_close ()
{
    PWL_LOG_INFO("MBIM Device close");

    mbim_device_close (device,
                       5,
                       NULL,
                       (GAsyncReadyCallback)mbim_device_close_ready,
                       NULL);

    g_clear_object (&device);
}

static void
device_open_ready (MbimDevice   *dev,
                   GAsyncResult *res)
{
    PWL_LOG_INFO("MBIM Device open");
    GError *error = NULL;

    if (!mbim_device_open_finish (dev, res, &error)) {
        PWL_LOG_ERR("error: couldn't open the MbimDevice: %s\n",
                    error->message);
        return;
    }

    PWL_LOG_INFO("MBIM Device at '%s' ready",
                  mbim_device_get_path_display (dev));

    if (g_ready_cb != NULL) {
        g_ready_cb();
    }
}

static void
device_new_ready (GObject      *unused,
                  GAsyncResult *res)
{
    pthread_mutex_unlock(&g_device_mutex);
    PWL_LOG_INFO("MBIM Device ready");

    GError *error = NULL;
    MbimDeviceOpenFlags open_flags = MBIM_DEVICE_OPEN_FLAGS_NONE;

    device = mbim_device_new_finish (res, &error);
    if (!device) {
        PWL_LOG_ERR("error: couldn't create MbimDevice: %s\n",
                    error->message);
        return;
    }

    /* Setup device open flags */
    if (device_open_proxy_flag)
        open_flags |= MBIM_DEVICE_OPEN_FLAGS_PROXY;
    if (device_open_ms_mbimex_v2_flag)
        open_flags |= MBIM_DEVICE_OPEN_FLAGS_MS_MBIMEX_V2;
    if (device_open_ms_mbimex_v3_flag)
        open_flags |= MBIM_DEVICE_OPEN_FLAGS_MS_MBIMEX_V3;

    /* Open the device */
    mbim_device_open_full (device,
                           open_flags,
                           (PWL_CMD_TIMEOUT_SEC - 1),
                           cancellable,
                           (GAsyncReadyCallback) device_open_ready,
                           NULL);
}

gboolean pwl_mbimdeviceadpt_init(mbim_device_ready_callback cb) {
    pthread_mutex_lock(&g_device_mutex);
    PWL_LOG_INFO("MBIM Device init");

    g_autoptr(GFile)   file = NULL;

    gchar port[20];
    memset(port, 0, sizeof(port));
    if (!pwl_find_mbim_port(port, sizeof(port))) {
        PWL_LOG_ERR("find mbim port fail at mbim device adpt init!");
        return FALSE;
    }

    /* Create new MBIM device */
    file = g_file_new_for_path (port);
    cancellable = g_cancellable_new ();

    /* Launch MbimDevice creation */
    mbim_device_new (file, cancellable, (GAsyncReadyCallback)device_new_ready, NULL);

    g_ready_cb = cb;

    return TRUE;
}

void pwl_mbimdeviceadpt_deinit() {
    pthread_mutex_lock(&g_device_mutex);
    PWL_LOG_INFO("MBIM Device deinit");

    device_close();

    if (cancellable)
        g_object_unref (cancellable);
    if (device)
        g_object_unref (device);

    cancellable = NULL;
    device = NULL;
    g_ready_cb = NULL;
}
#endif /* AT_OVER_MBIM_API */
