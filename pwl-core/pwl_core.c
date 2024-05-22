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

static gboolean device_open_proxy_flag = TRUE;
static gboolean device_open_ms_mbimex_v2_flag = FALSE;
static gboolean device_open_ms_mbimex_v3_flag = FALSE;
static MbimDevice *device;
static GCancellable *cancellable;

// For GPIO reset
// int g_check_fastboot_retry_count;
// int g_wait_modem_port_retry_count;
// int g_wait_at_port_retry_count;
int g_fw_update_retry_count;
int g_do_hw_reset_count;
int g_need_retry_fw_update;

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

static gboolean gpio_reset_method(pwlCore     *object,
                           GDBusMethodInvocation *invocation) {
    get_fw_update_status_value(DO_HW_RESET_COUNT, &g_do_hw_reset_count);
    if (g_do_hw_reset_count <= HW_RESET_RETRY_TH) {
        g_do_hw_reset_count++;
        set_fw_update_status_value(DO_HW_RESET_COUNT, g_do_hw_reset_count);
        hw_reset();
    } else {
        PWL_LOG_ERR("Reached HW reset retry limit!!! (%d,%d)", g_fw_update_retry_count, g_do_hw_reset_count);
    }
    return TRUE;
}

//static gboolean request_retry_fw_update_method(pwlCore     *object,
//                           GDBusMethodInvocation *invocation) {
//    // Check fw update retry count
//    if (g_fw_update_retry_count <= FW_UPDATE_RETRY_TH) {
//        PWL_LOG_DEBUG("Send retry fw update request signal.");
//        g_fw_update_retry_count++;
//        set_fw_update_status_value(FW_UPDATE_RETRY_COUNT, g_fw_update_retry_count);
//        set_fw_update_status_value(NEED_RETRY_FW_UPDATE, 0);
//        pwl_core_emit_request_retry_fw_update_signal(gp_skeleton);
//    } else {
//        PWL_LOG_ERR("Reach fw update retry limit (3 times), abort update!");
//    }
//    return TRUE;
//}

static gboolean hw_reset() {
    PWL_LOG_DEBUG("!!=== Do GPIO reset ===!!");
    int ret = 0;
    FILE *fp = NULL;
    char system_cmd[128];
    char SKU_id[16];
    int search_array_len = sizeof(g_skuid_to_gpio) / sizeof(s_skuid_to_gpio);
    int i = 0;
    int gpio;

    if(0 == access("/sys/class/gpio/export", F_OK)) {
        PWL_LOG_DEBUG("/sys/class/gpio/export exists");
    } else {
        PWL_LOG_DEBUG("/sys/class/gpio/export does not exist");
        return -1;
    }

    sprintf(system_cmd,"dmidecode -t 1 | grep SKU | awk -F ' ' '{print$3}'");
    fp = popen(system_cmd, "r");
    if(fp == NULL){
        PWL_LOG_ERR("gpio reset system_cmd dmidecode error");
        return -1;
    }

    ret = fread(SKU_id, sizeof(char), sizeof(SKU_id), fp);
    if(ret <= 0){
        PWL_LOG_ERR("gpio reset fread error");
        return -1;
    }

    PWL_LOG_DEBUG("gpio reset SKU_id:%s", SKU_id);

    pclose(fp);
    fp = NULL;

    for (i = 0; i < search_array_len; ++i){
        if(strstr(SKU_id, g_skuid_to_gpio[i].skuid) == NULL){
            continue;
        }else{
            gpio = g_skuid_to_gpio[i].gpio;
            break;
        }
    }

    if(i == search_array_len){
        PWL_LOG_ERR("gpio reset don't find skuid form table");
        return -1;
    }

    // Disable gpio
    if (set_gpio_status(0, gpio) != 0)
    {
        PWL_LOG_ERR("Disable GPIO error");
        return -1;
    }
    sleep(1);

    // Enable gpio
    if (set_gpio_status(1, gpio) != 0)
    {
        PWL_LOG_ERR("Enable GPIO error");
        return -1;
    }

    // restart madpt for module port init
    sleep(5);
    send_message_queue(PWL_CID_MADPT_RESTART);

    return TRUE;
}

int set_gpio_status(int enable, int gpio) {
    FILE *fp;
    char gpio_cmd[64] = {0};

    if (enable == 1 || enable == 0) {
        sprintf(gpio_cmd, "echo %d > /sys/class/gpio/gpio%d/value", enable, gpio);
    } else {
        PWL_LOG_ERR("gpio incorrect value %d", enable);
        return -1;
    }

    fp = popen(gpio_cmd, "r");
    if (fp == NULL) {
        PWL_LOG_DEBUG("gpio cmd error");
        return -1;
    }

    pclose(fp);
    return 0;
}

int gpio_init()
{
    FILE *fp = NULL;
    char system_cmd[64] = {0};
    char SKU_id[16];
    int search_array_len = sizeof(g_skuid_to_gpio) / sizeof(s_skuid_to_gpio);
    int i, gpio;
    int ret = -1;

    if(0 == access("/sys/class/gpio/export", F_OK)) {
        PWL_LOG_ERR("/sys/class/gpio/export exists");
    } else {
        PWL_LOG_ERR("/sys/class/gpio/export does not exist");
        return -1;
    }

    sprintf(system_cmd, "dmidecode -t 1 | grep SKU | awk -F ' ' '{print$3}'");
    fp = popen(system_cmd, "r");
    if(fp == NULL){
        PWL_LOG_ERR("gpio init system_cmd dmidecode error");
        return -1;
    }

    ret = fread(SKU_id, sizeof(char), sizeof(SKU_id), fp);
    if(ret <= 0){
        PWL_LOG_ERR("gpio init fread error");
        pclose(fp);
        return -1;
    }

    PWL_LOG_DEBUG("gpio init SKU_id:%s", SKU_id);
    pclose(fp);
    fp = NULL;

    for (i = 0; i < search_array_len; ++i) {
        if (strstr(SKU_id, g_skuid_to_gpio[i].skuid) == NULL) {
            continue;
        } else {
            gpio = g_skuid_to_gpio[i].gpio;
            break;
        }
    }

    if(i == search_array_len)
    {
        PWL_LOG_ERR("gpio init don't find skuid form table");
        return -1;
    }

    sprintf(system_cmd, "echo %d > /sys/class/gpio/export", gpio);
    fp = popen(system_cmd, "w");
    if(fp == NULL){
        PWL_LOG_ERR("gpio init system_cmd gpio export error");
        return -1;
    }

    if (set_gpio_status(1, gpio) != 0)
    {
        PWL_LOG_ERR("gpio init system_cmd gpio value error");
        return -1;
    }

    sprintf(system_cmd, "echo out > /sys/class/gpio/gpio%d/direction", gpio);
    ret = fwrite(system_cmd, sizeof(char), strlen(system_cmd) + 1, fp);
    if (ret == 0) {
        PWL_LOG_ERR("gpio init system_cmd set gpio direction error");
        return -1;
    }
    pclose(fp);
    fp = NULL;
}

static void bus_acquired_hdl(GDBusConnection *connection,
                             const gchar     *bus_name,
                             gpointer         user_data) {
    GError *pError = NULL;

    /** Second step: Try to get a connection to the given bus. */
    gp_skeleton = pwl_core_skeleton_new();

    /** Third step: Attach to dbus signals. */
    (void) g_signal_connect(gp_skeleton, "handle-madpt-ready-method", G_CALLBACK(madpt_ready_method), NULL);
    (void) g_signal_connect(gp_skeleton, "handle-ready-to-fcc-unlock-method", G_CALLBACK(ready_to_fcc_unlock_method), NULL);
    (void) g_signal_connect(gp_skeleton, "handle-gpio-reset-method", G_CALLBACK(gpio_reset_method), NULL);
    //(void) g_signal_connect(gp_skeleton, "handle-request-retry-fw-update-method", G_CALLBACK(request_retry_fw_update_method), NULL);

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

static gboolean
common_process_register_state (MbimDevice            *device,
                               MbimMessage           *message,
                               MbimNwError           *out_nw_error,
                               GError               **error)
{
    MbimNwError        nw_error = 0;
    MbimRegisterState  register_state = MBIM_REGISTER_STATE_UNKNOWN;
    MbimDataClass      available_data_classes = 0;
    g_autofree gchar  *provider_id = NULL;
    g_autofree gchar  *provider_name = NULL;
    MbimDataClass      preferred_data_classes = 0;
    const gchar       *nw_error_str;
    g_autofree gchar  *available_data_classes_str = NULL;
    g_autofree gchar  *preferred_data_classes_str = NULL;
    gboolean           is_notification;

    is_notification = (mbim_message_get_message_type (message) == MBIM_MESSAGE_TYPE_INDICATE_STATUS);
    g_assert (is_notification || (mbim_message_get_message_type (message) == MBIM_MESSAGE_TYPE_COMMAND_DONE));

    if (mbim_device_check_ms_mbimex_version (device, 2, 0)) {
        if (is_notification) {
            if (!mbim_message_ms_basic_connect_v2_register_state_notification_parse (
                    message,
                    &nw_error,
                    &register_state,
                    NULL, /* register_mode */
                    &available_data_classes,
                    NULL, /* current_cellular_class */
                    &provider_id,
                    &provider_name,
                    NULL, /* roaming_text */
                    NULL, /* registration_flag */
                    &preferred_data_classes,
                    error)) {
                PWL_LOG_ERR("Failed processing MBIMEx v2.0 register state indication: ");
                return FALSE;
            }
            PWL_LOG_DEBUG("processed MBIMEx v2.0 register state indication");
        } else {
            if (!mbim_message_ms_basic_connect_v2_register_state_response_parse (
                    message,
                    &nw_error,
                    &register_state,
                    NULL, /* register_mode */
                    &available_data_classes,
                    NULL, /* current_cellular_class */
                    &provider_id,
                    &provider_name,
                    NULL, /* roaming_text */
                    NULL, /* registration_flag */
                    &preferred_data_classes,
                    error)) {
                PWL_LOG_ERR("Failed processing MBIMEx v2.0 register state response: ");
                return FALSE;
            }
            PWL_LOG_DEBUG("processed MBIMEx v2.0 register state indication");
        }
    } else {
        if (is_notification) {
            if (!mbim_message_register_state_notification_parse (
                    message,
                    &nw_error,
                    &register_state,
                    NULL, /* register_mode */
                    &available_data_classes,
                    NULL, /* current_cellular_class */
                    &provider_id,
                    &provider_name,
                    NULL, /* roaming_text */
                    NULL, /* registration_flag */
                    error)) {
                PWL_LOG_ERR("Failed processing register state indication: ");
                return FALSE;
            }
            PWL_LOG_DEBUG("processed register state indication");
        } else {
            if (!mbim_message_register_state_response_parse (
                    message,
                    &nw_error,
                    &register_state,
                    NULL, /* register_mode */
                    &available_data_classes,
                    NULL, /* current_cellular_class */
                    &provider_id,
                    &provider_name,
                    NULL, /* roaming_text */
                    NULL, /* registration_flag */
                    error)) {
                PWL_LOG_ERR("Failed processing register state response: ");
                return FALSE;
            }
            PWL_LOG_DEBUG("processed register state response");
        }
    }

    //nw_error = mm_broadband_modem_mbim_normalize_nw_error (self, nw_error);
    //nw_error_str = mbim_nw_error_get_string (nw_error);
    available_data_classes_str = mbim_data_class_build_string_from_mask (available_data_classes);
    preferred_data_classes_str = mbim_data_class_build_string_from_mask (preferred_data_classes);

    PWL_LOG_DEBUG("register state update:");
    //if (nw_error_str)
    //    PWL_LOG_DEBUG("              nw error: '%s'", nw_error_str);
    //else
    //    PWL_LOG_DEBUG("              nw error: '0x%x'", nw_error);
    PWL_LOG_DEBUG("                 state: '%s'", mbim_register_state_get_string (register_state));
    PWL_LOG_DEBUG("           provider id: '%s'", provider_id ? provider_id : "n/a");
    PWL_LOG_DEBUG("         provider name: '%s'", provider_name ? provider_name : "n/a");
    PWL_LOG_DEBUG("available data classes: '%s'", available_data_classes_str);
    PWL_LOG_DEBUG("preferred data classes: '%s'", preferred_data_classes_str);


    if (out_nw_error)
        *out_nw_error = nw_error;
    return TRUE;
}

static void
basic_connect_notification_subscriber_ready_status (MbimDevice           *device,
                                                    MbimMessage          *notification)
{
    MbimSubscriberReadyState ready_state;
    g_auto(GStrv)            telephone_numbers = NULL;
    g_autoptr(GError)        error = NULL;
    gboolean                 active_sim_event = FALSE;

    if (mbim_device_check_ms_mbimex_version (device, 3, 0)) {
        if (!mbim_message_ms_basic_connect_v3_subscriber_ready_status_notification_parse (
                notification,
                &ready_state,
                NULL, /* flags */
                NULL, /* subscriber id */
                NULL, /* sim_iccid */
                NULL, /* ready_info */
                NULL, /* telephone_numbers_count */
                &telephone_numbers,
                &error)) {
            PWL_LOG_ERR("Failed processing MBIMEx v3.0 subscriber ready status notification: %s", error->message);
            return;
        }
        PWL_LOG_DEBUG("processed MBIMEx v3.0 subscriber ready status notification");
    } else {
        if (!mbim_message_subscriber_ready_status_notification_parse (
                notification,
                &ready_state,
                NULL, /* subscriber_id */
                NULL, /* sim_iccid */
                NULL, /* ready_info */
                NULL, /* telephone_numbers_count */
                &telephone_numbers,
                &error)) {
            PWL_LOG_ERR("Failed processing subscriber ready status notification: %s", error->message);
            return;
        }
        PWL_LOG_DEBUG("processed subscriber ready status notification");
    }

    switch (ready_state) {
    case MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED:
        PWL_LOG_DEBUG("MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED");
        break;
    case MBIM_SUBSCRIBER_READY_STATE_INITIALIZED:
        PWL_LOG_DEBUG("MBIM_SUBSCRIBER_READY_STATE_INITIALIZED");
        break;
    case MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED:
        PWL_LOG_DEBUG("MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED");
        break;
    case MBIM_SUBSCRIBER_READY_STATE_BAD_SIM:
        PWL_LOG_DEBUG("MBIM_SUBSCRIBER_READY_STATE_BAD_SIM");
        break;
    case MBIM_SUBSCRIBER_READY_STATE_FAILURE:
        PWL_LOG_DEBUG("MBIM_SUBSCRIBER_READY_STATE_FAILURE");
        break;
    case MBIM_SUBSCRIBER_READY_STATE_NOT_ACTIVATED:
        PWL_LOG_DEBUG("MBIM_SUBSCRIBER_READY_STATE_NOT_ACTIVATED");
        break;
    case MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED:
        PWL_LOG_DEBUG("MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED");
        break;
    case MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE:
        PWL_LOG_DEBUG("MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE");
        break;
    default:
        PWL_LOG_ERR("ready state unknown");
        break;
    }

    pwl_core_emit_subscriber_ready_state_change(gp_skeleton);

#if (0)
    if (ready_state == MBIM_SUBSCRIBER_READY_STATE_INITIALIZED)
        mm_iface_modem_update_own_numbers (MM_IFACE_MODEM (self), telephone_numbers);

    if ((self->priv->enabled_cache.last_ready_state != MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE &&
         ready_state == MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE) ||
        (self->priv->enabled_cache.last_ready_state == MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE &&
         ready_state != MBIM_SUBSCRIBER_READY_STATE_NO_ESIM_PROFILE)) {
        /* eSIM profiles have been added or removed, re-probe to ensure correct interfaces are exposed */
        PWL_LOG_DEBUG("eSIM profile updates detected");
        active_sim_event = TRUE;
    }

    if ((self->priv->enabled_cache.last_ready_state != MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED &&
         ready_state == MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED) ||
        (self->priv->enabled_cache.last_ready_state == MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED &&
         ready_state != MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED)) {
        /* SIM has been removed or reinserted, re-probe to ensure correct interfaces are exposed */
        PWL_LOG_DEBUG("SIM hot swap detected");
        active_sim_event = TRUE;
    }

    if ((self->priv->enabled_cache.last_ready_state != MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED &&
         ready_state == MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED) ||
        (self->priv->enabled_cache.last_ready_state == MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED &&
         ready_state != MBIM_SUBSCRIBER_READY_STATE_DEVICE_LOCKED)) {
        g_autoptr(MbimMessage) message = NULL;

        /* Query which lock has changed */
        message = mbim_message_pin_query_new (NULL);
        mbim_device_command (device,
                             message,
                             10,
                             NULL,
                             (GAsyncReadyCallback)pin_query_after_subscriber_ready_status_ready,
                             g_object_ref (self));
    }

    /* Ignore NOT_INITIALIZED state when setting the last_ready_state as it is
     * reported regardless of whether SIM was inserted or unlocked */
    if (ready_state != MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED) {
        self->priv->enabled_cache.last_ready_state = ready_state;
    }

    if (active_sim_event) {
        mm_iface_modem_process_sim_event (MM_IFACE_MODEM (self));
    }
#endif
}

static void
basic_connect_notification_register_state (MbimDevice           *device,
                                           MbimMessage          *notification)
{
    g_autoptr(GError)  error = NULL;

    if (!common_process_register_state (device, notification, NULL, &error))
        PWL_LOG_ERR("%s", error->message);
}

static void
basic_connect_notification (MbimDevice           *device,
                            MbimMessage          *notification)
{
    switch (mbim_message_indicate_status_get_cid (notification)) {
    case MBIM_CID_BASIC_CONNECT_SIGNAL_STATE:
        PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_SIGNAL_STATE");
        break;
    case MBIM_CID_BASIC_CONNECT_REGISTER_STATE:
        PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_REGISTER_STATE");
        basic_connect_notification_register_state (device, notification);
        break;
    case MBIM_CID_BASIC_CONNECT_CONNECT:
        PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_CONNECT");
        break;
    case MBIM_CID_BASIC_CONNECT_SUBSCRIBER_READY_STATUS:
        PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_SUBSCRIBER_READY_STATUS");
        basic_connect_notification_subscriber_ready_status (device, notification);
        break;
    case MBIM_CID_BASIC_CONNECT_PACKET_SERVICE:
        PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_PACKET_SERVICE");
        break;
    case MBIM_CID_BASIC_CONNECT_PROVISIONED_CONTEXTS:
        PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_PROVISIONED_CONTEXTS");
    case MBIM_CID_BASIC_CONNECT_IP_CONFIGURATION:
        PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_IP_CONFIGURATION");
        /* Ignored at modem level, only managed by bearer if waiting for async SLAAC results */
    default:
        PWL_LOG_ERR("basic connect indicate cid unknown");
        break;
    }
}

static void
ms_basic_connect_extensions_notification (MbimDevice           *device,
                                          MbimMessage          *notification)
{
    switch (mbim_message_indicate_status_get_cid (notification)) {
    case MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_PCO:
        PWL_LOG_DEBUG("MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_PCO");
        break;
    case MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_LTE_ATTACH_INFO:
        PWL_LOG_DEBUG("MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_LTE_ATTACH_INFO");
        break;
    case MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_SLOT_INFO_STATUS:
        PWL_LOG_DEBUG("MBIM_CID_MS_BASIC_CONNECT_EXTENSIONS_SLOT_INFO_STATUS");
        break;
    default:
        PWL_LOG_ERR("ms basic connect extensions indicate cid unknown");
        break;
    }
}

static void
mbim_indication_cb (MbimDevice *device,
                    MbimMessage *notification)
{
    MbimService  service;

    service = mbim_message_indicate_status_get_service (notification);

    PWL_LOG_DEBUG("received notification (service '%s', command '%s')",
                  mbim_service_get_string (service),
                  mbim_cid_get_printable (service,
                                          mbim_message_indicate_status_get_cid (notification)));

    if (service == MBIM_SERVICE_BASIC_CONNECT)
        basic_connect_notification (device, notification);
    else if (service == MBIM_SERVICE_MS_BASIC_CONNECT_EXTENSIONS)
        ms_basic_connect_extensions_notification (device, notification);
}

static void
device_open_ready (MbimDevice   *dev,
                   GAsyncResult *res)
{
    GError *error = NULL;

    if (!mbim_device_open_finish (dev, res, &error)) {
        PWL_LOG_ERR("error: couldn't open the MbimDevice: %s\n",
                    error->message);
        return;
    }

    PWL_LOG_DEBUG("MBIM Device at '%s' ready",
                  mbim_device_get_path_display (dev));
#if (0) //TODO
    g_signal_connect (device,
                      MBIM_DEVICE_SIGNAL_REMOVED,
                      G_CALLBACK (mbim_device_removed_cb),
                      NULL);

    g_signal_connect (device,
                      MBIM_DEVICE_SIGNAL_ERROR,
                      G_CALLBACK (mbim_device_error_cb),
                      NULL);
#endif
    g_signal_connect (device,
                      MBIM_DEVICE_SIGNAL_INDICATE_STATUS,
                      G_CALLBACK (mbim_indication_cb),
                      NULL);
}


static void
device_new_ready (GObject      *unused,
                  GAsyncResult *res)
{
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
                           30,
                           cancellable,
                           (GAsyncReadyCallback) device_open_ready,
                           NULL);
}

gboolean mbim_dbus_init(void) {

    g_autoptr(GFile)   file = NULL;

    gchar port[20];
    memset(port, 0, sizeof(port));
    if (!pwl_find_mbim_port(port, sizeof(port))) {
        PWL_LOG_ERR("find mbim port fail at gdbus init!");
        return FALSE;
    }

    /* Create new MBIM device */
    file = g_file_new_for_path (port);
    cancellable = g_cancellable_new ();

    /* Launch MbimDevice creation */
    mbim_device_new (file, cancellable, (GAsyncReadyCallback)device_new_ready, NULL);

    return TRUE;
}

static gpointer mbim_device_thread(gpointer data) {
    while (!mbim_dbus_init()) {
        sleep(5);
    }
    PWL_LOG_INFO("mbim device open start");
    return ((void*)0);
}

gint main() {
    PWL_LOG_INFO("start");

    GThread *mbim_thread = g_thread_new("mbim_thread", mbim_device_thread, NULL);

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

    // FW update status init
    if (fw_update_status_init() == 0) {
        // get_fw_update_status_value(FIND_FASTBOOT_RETRY_COUNT, &g_check_fastboot_retry_count);
        // get_fw_update_status_value(WAIT_MODEM_PORT_RETRY_COUNT, &g_wait_modem_port_retry_count);
        // get_fw_update_status_value(WAIT_AT_PORT_RETRY_COUNT, &g_wait_at_port_retry_count);
        get_fw_update_status_value(FW_UPDATE_RETRY_COUNT, &g_fw_update_retry_count);
        get_fw_update_status_value(DO_HW_RESET_COUNT, &g_do_hw_reset_count);
        get_fw_update_status_value(NEED_RETRY_FW_UPDATE, &g_need_retry_fw_update);
    }

    gpio_init();

    gp_loop = g_main_loop_new(NULL, FALSE);

    g_main_loop_run(gp_loop);

    if (cancellable)
        g_object_unref (cancellable);
    if (device)
        g_object_unref (device);

    // de-init
    if (0 != gp_loop) {
        g_main_loop_quit(gp_loop);
        g_main_loop_unref(gp_loop);
    }

    return 0;
}
