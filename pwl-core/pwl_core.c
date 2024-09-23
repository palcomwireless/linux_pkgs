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

extern gchar* pcieid_info[];

#define AUTOSUSPEND_DELAY_NODE_PATH     "/sys/bus/pci/devices/%s/power/autosuspend_delay_ms"
#define AUTOSUSPEND_DELAY_VALUE         "5000"

static gpointer mbim_device_thread(gpointer data);

static GMainLoop *gp_loop;
static pwlCore *gp_skeleton = NULL;

static MbimDevice *g_device;
static GCancellable *g_cancellable;
static MbimDevice *g_pci_device;
static GCancellable *g_pci_cancellable;

// For GPIO reset
// int g_check_fastboot_retry_count;
// int g_wait_modem_port_retry_count;
// int g_wait_at_port_retry_count;
int g_fw_update_retry_count;
int g_do_hw_reset_count;
int g_need_retry_fw_update;

// For PCI hw reset
mbim_device_ready_callback g_ready_cb;
pthread_mutex_t g_device_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_device_cond = PTHREAD_COND_INITIALIZER;
int g_mode = 0;
int g_rescan_failure_count = 0;
gboolean g_device_cap_check_pass = FALSE;
gboolean g_hw_reset_check = FALSE;
int g_bootup_failure_count;
int g_bootup_failure_limit;

int do_shell_cmd(char *cmd, char *response) {
    FILE *fp = NULL;
    char buffer[SHELL_CMD_RSP_LENGTH];

    fp = popen(cmd, "r");
    if (fp == NULL)
        return RET_FAILED;
    if (DEBUG) PWL_LOG_DEBUG("cmd: %s", cmd);
    memset(buffer, 0, sizeof(buffer));
    char *ret = fgets(buffer, sizeof(buffer), fp);
    strcpy(response, buffer);
    pclose(fp);
    return RET_OK;
}

int get_full_path(char *full_path) {
    char command[SHELL_CMD_RSP_LENGTH];
    char rsp[SHELL_CMD_RSP_LENGTH];
    memset(rsp, 0, sizeof(rsp));
    sprintf(command, "find /sys/ -name %s", DEVICE_MODE_NAME);
    do_shell_cmd(command, rsp);
    if (strlen(rsp) > 0) {
        strcpy(full_path, rsp);
        return RET_OK;
    } else {
        return RET_FAILED;
    }
}
int get_device_node_path(char *full_path, char *device_node_path) {
    int full_patch_size = strlen(full_path);
    char temp[full_patch_size + 1];
    memset(temp, 0, sizeof(temp));
    strcpy(temp, full_path);
    char *token = strtok(temp, "/");
    while (token != NULL) {
        if (strstr(token, DEVICE_MODE_NAME)) {
            strcat(device_node_path, "/");
            break;
        }
        strcat(device_node_path, "/");
        strcat(device_node_path, token);
        token = strtok(NULL, "/");
    }
    return RET_OK;
}

int get_device_mode(char *device_node_path, char *mode) {
    char buffer[DEVICE_MODE_LENGTH];
    char file_name[SHELL_CMD_RSP_LENGTH];
    sprintf(file_name, "%s%s", device_node_path, DEVICE_MODE_NAME);

    PWL_LOG_DEBUG("Get mode from: %s", file_name);

    FILE *fp = fopen(file_name, "rb");
    if (fp) {
        size_t read_bytes = fread(buffer, 1, DEVICE_MODE_LENGTH, fp);
        strcpy(mode, buffer);
        fclose(fp);
        return RET_OK;
    } else {
        PWL_LOG_ERR("File open fail!");
    }
    return RET_FAILED;
}

int set_device_mode(char *device_node_path, char *node, char *value) {
    // echo "reset" > ${device_path}t7xx_mode
    char file_name[SHELL_CMD_RSP_LENGTH];
    sprintf(file_name, "%s%s", device_node_path, node);

    FILE *fp = fopen(file_name, "w");
    if (fp) {
        PWL_LOG_DEBUG("Set %s to %s", value, file_name);
        fprintf(fp, "%s", value);
        fclose(fp);
        return RET_OK;
    } else {
        PWL_LOG_ERR("File open fail!");
    }
    return RET_FAILED;
}

int do_pci_hw_reset(int reset_mode) {
    //full_path
    char full_path[SHELL_CMD_RSP_LENGTH];
    memset(full_path, 0, sizeof(full_path));
    if (get_full_path(full_path) == RET_OK)
        PWL_LOG_DEBUG("Full path: %s", full_path);
    else {
        PWL_LOG_ERR("Get full path error!");
        return RET_FAILED;
    }

    //device node path
    char device_node_path[SHELL_CMD_RSP_LENGTH];
    memset(device_node_path, 0, sizeof(device_node_path));
    if (get_device_node_path(full_path, device_node_path) == RET_OK) {
        PWL_LOG_DEBUG("Device node: %s", device_node_path);
    } else {
        PWL_LOG_ERR("Get device node path error!");
        return RET_FAILED;
    }

    // Get device mode
    char device_mode[DEVICE_MODE_LENGTH];
    memset(device_mode, 0, sizeof(device_mode));
    if (get_device_mode(device_node_path, device_mode) == RET_OK) {
        PWL_LOG_DEBUG("Device mode: %s", device_mode);
    } else {
        PWL_LOG_ERR("Get Device mode error!");
        return RET_FAILED;
    }

    //Write reset to t7xx_mode
    if (reset_mode == DEVICE_HW_RESET) {
        set_device_mode(device_node_path, DEVICE_MODE_NAME, "reset");
        PWL_LOG_DEBUG("Sleep %d secs", DEVICE_REMOVE_DELAY);
        for (int i = 0; i < DEVICE_REMOVE_DELAY; i++) {
            if (DEBUG) PWL_LOG_DEBUG("Sleep %i", i);
            sleep(1);
        }
        memset(device_mode, 0, sizeof(device_mode));
        if (get_device_mode(device_node_path, device_mode) == RET_OK) {
            PWL_LOG_DEBUG("Device mode: %s", device_mode);
        } else {
            PWL_LOG_ERR("Get Device mode error!");
            return RET_FAILED;
        }
    }

    //Remove device
    PWL_LOG_INFO("Remove device from pci");
    set_device_mode(device_node_path, DEVICE_REMOVE_NAME, "1");
    PWL_LOG_DEBUG("Sleep %d secs", DEVICE_RESCAN_DELAY);
    for (int i = 0; i < DEVICE_RESCAN_DELAY; i++) {
        if (DEBUG) PWL_LOG_DEBUG("Sleep %i", i);
        sleep(1);
    }

    //Rescan device
    PWL_LOG_INFO("Rescan pci");
    set_device_mode("/sys/bus/pci/", DEVICE_RESCAN_NAME, "1");
    PWL_LOG_DEBUG("Sleep 30 secs");
    for (int i = 0; i < 30; i++) {
        if (DEBUG) PWL_LOG_DEBUG("Sleep %i", i);
        sleep(1);
    }
    PWL_LOG_INFO("Rescan done");

    memset(device_mode, 0, sizeof(device_mode));
    if (get_device_mode(device_node_path, device_mode) == RET_OK) {
        PWL_LOG_DEBUG("Device mode: %s", device_mode);
    } else {
        PWL_LOG_ERR("Get Device mode error!");
        return RET_FAILED;
    }
    return RET_OK;
}

static void pci_mbim_device_close_cb(MbimDevice *dev, GAsyncResult *res) {
    PWL_LOG_INFO("MBIM Device close cb");
    GError *error = NULL;

    if (!mbim_device_close_finish(dev, res, &error))
        g_error_free(error);

    pthread_cond_signal(&g_device_cond);
}

static void pci_device_close() {
    PWL_LOG_INFO("MBIM Device close");

    mbim_device_close(g_pci_device, PWL_CLOSE_MBIM_TIMEOUT_SEC, NULL,
                     (GAsyncReadyCallback) pci_mbim_device_close_cb, NULL);

    g_clear_object(&g_pci_device);
}

static void pci_device_open_cb(MbimDevice *dev, GAsyncResult *res) {
    PWL_LOG_INFO("MBIM Device open");
    g_autoptr(GError) error = NULL;

    if (!mbim_device_open_finish(dev, res, &error)) {
        PWL_LOG_ERR("Couldn't open Mbim Device: %s\n", error->message);
        return;
    }

    PWL_LOG_DEBUG("MBIM Device %s opened.", mbim_device_get_path_display(dev));

    // PWL_LOG_DEBUG("Is open: %d", mbim_device_is_open(dev));
    if (g_ready_cb != NULL) {
        g_ready_cb();
    }
}

static void pci_device_new_cb(GObject *unused, GAsyncResult *res) {
    // pthread_mutex_unlock(&g_device_mutex);
    PWL_LOG_DEBUG("== MBIM Device ready ==");

    g_autoptr(GError) error = NULL;

    g_pci_device = mbim_device_new_finish(res, &error);
    if (!g_pci_device) {
        PWL_LOG_ERR("Couldn't create MbimDevice object: %s\n", error->message);
        return;
    }

    mbim_device_open_full(g_pci_device, MBIM_DEVICE_OPEN_FLAGS_PROXY,
                          (TIMEOUT_SEC - 1), g_pci_cancellable,
                          (GAsyncReadyCallback) pci_device_open_cb, NULL);
}

gboolean find_mbim_port(gchar *port_buff_ptr, guint32 port_buff_size) {
    FILE *fp = popen("find /dev/ -name wwan0mbim*", "r");

    if (fp == NULL) {
        PWL_LOG_ERR("find port cmd error!!!");
        return RET_FAILED;
    }

    char buffer[50];
    memset(buffer, 0, sizeof(buffer));
    char *ret = fgets(buffer, sizeof(buffer), fp);
    pclose(fp);

    buffer[strcspn(buffer, "\n")] = 0;

    if ((strlen(buffer) + 1) > port_buff_size) {
        PWL_LOG_ERR("port buffer size %d not enough!!!", port_buff_size);
        return RET_FAILED;
    }

    if (strlen(buffer) <= 0)
        return RET_FAILED;

    strncpy(port_buff_ptr, buffer, strlen(buffer));

    return RET_OK;
}

gboolean find_abnormal_port(gchar *port_buff_ptr, guint32 port_buff_size) {
    FILE *fp = popen("find /dev/ -name wwan0fastboot*", "r");

    if (fp == NULL) {
        PWL_LOG_ERR("find port cmd error!!!");
        return RET_FAILED;
    }

    char buffer[50];
    memset(buffer, 0, sizeof(buffer));
    char *ret = fgets(buffer, sizeof(buffer), fp);
    pclose(fp);

    buffer[strcspn(buffer, "\n")] = 0;

    if ((strlen(buffer) + 1) > port_buff_size) {
        PWL_LOG_ERR("port buffer size %d not enough!!!", port_buff_size);
        return RET_FAILED;
    }

    if (strlen(buffer) <= 0)
        return RET_FAILED;

    strncpy(port_buff_ptr, buffer, strlen(buffer));
    PWL_LOG_DEBUG("Found abnormal port: %s", port_buff_ptr);
    return RET_OK;
}

gboolean pci_mbim_device_init(mbim_device_ready_callback cb) {
    // pthread_mutex_lock(&g_device_mutex);
    PWL_LOG_DEBUG("== MBIM Device init ==");

    g_autoptr(GFile) file = NULL;
    gchar port [20];
    memset(port, 0, sizeof(port));
    //Check if mbim port exist
    if (RET_FAILED == find_mbim_port(port, sizeof(port))) {
        PWL_LOG_ERR("Find mbim port fail!");
        return RET_FAILED;
    }
    PWL_LOG_DEBUG("mbim port: %s", port);

    file = g_file_new_for_path(port);
    g_pci_cancellable = g_cancellable_new();

    mbim_device_new(file, g_pci_cancellable,
                   (GAsyncReadyCallback) pci_device_new_cb, NULL);
    g_ready_cb = cb;

    return RET_OK;
}

void pci_mbim_device_deinit(void) {
    PWL_LOG_ERR("MBIM Device deinit");

    pci_device_close();
    if (!cond_wait(&g_device_mutex, &g_device_cond, PWL_CLOSE_MBIM_TIMEOUT_SEC)) {
        if (DEBUG) PWL_LOG_ERR("timed out or error during mbim deinit");
    }

    if (g_pci_cancellable)
        g_object_unref(g_pci_cancellable);
    if (g_pci_device)
        g_object_unref(g_pci_device);

    g_pci_cancellable = NULL;
    g_pci_device = NULL;
    // g_ready_cb = NULL;
}

static void device_caps_cb(MbimDevice *dev, GAsyncResult *res) {
    PWL_LOG_DEBUG("== Device Caps CB ===");
    g_autoptr(MbimMessage) response = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *out_device_id = NULL;
    g_autofree gchar *out_firmware_info = NULL;
    g_autofree gchar *out_hardware_info = NULL;

    response = mbim_device_command_finish(dev, res, &error);

    mbim_message_device_caps_response_parse(response, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL,
                                            &out_device_id, &out_firmware_info,
                                            &out_hardware_info, &error);

    if (DEBUG) PWL_LOG_DEBUG("[device_id]: %s", out_device_id);
    if (DEBUG) PWL_LOG_DEBUG("[firmware_info]: %s", out_firmware_info);
    if (DEBUG) PWL_LOG_DEBUG("[hardware_info]: %s", out_hardware_info);
    //Check device info correct
    g_device_cap_check_pass = FALSE;
    if (out_firmware_info != NULL && out_hardware_info != NULL) {
        if (strlen(out_firmware_info) > 0 && strlen(out_hardware_info) > 0) {
            g_device_cap_check_pass = TRUE;
            PWL_LOG_INFO("Device cap info responsed");
        }
    }
}

static void pci_mbim_device_ready_cb() {
    PWL_LOG_DEBUG("== Ready CB ==");
    g_autoptr(MbimMessage) message = NULL;
    g_autoptr(GError) error = NULL;

    switch (g_mode) {
        case MODE_DEVICE_CAP:
            PWL_LOG_DEBUG("Mode: get device caps");
            message = mbim_message_device_caps_query_new(&error);
            mbim_device_command(g_pci_device, message, 10, NULL,
                               (GAsyncReadyCallback)device_caps_cb, &error);
            break;

        default:
            break;
    }
}

int check_module_info_v2() {
    int check_result;
    g_mode = MODE_DEVICE_CAP;
    g_device_cap_check_pass = FALSE;
    if (RET_FAILED == pci_mbim_device_init(pci_mbim_device_ready_cb)) {
        PWL_LOG_ERR("Module cap info check fail!");
        pci_mbim_device_deinit();
        return RET_FAILED;
    }
    sleep(3);
    // Check module cap info
    if (g_device_cap_check_pass) {
        PWL_LOG_DEBUG("Check module cap pass");
        check_result = RET_OK;
    } else {
        PWL_LOG_ERR("Check module cap fail");
        check_result = RET_FAILED;
    }
    pci_mbim_device_deinit();
    return check_result;
}

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

static gboolean madpt_ready_method(pwlCore     *object,
                           GDBusMethodInvocation *invocation) {

    PWL_LOG_DEBUG("Madpt ready, send signal to get FW version!");
    pwl_core_emit_get_fw_version_signal(gp_skeleton);
    return TRUE;

}

static gboolean request_update_fw_version(pwlCore     *object,
                           GDBusMethodInvocation *invocation) {

    PWL_LOG_DEBUG("Received request, send signal to get FW version!");
    pwl_core_emit_get_fw_version_signal(gp_skeleton);
    return TRUE;
}

static gboolean request_fw_update_check(pwlCore     *object,
                           GDBusMethodInvocation *invocation) {

    PWL_LOG_DEBUG("Received request, send signal do fw update check");
    pwl_core_emit_get_fw_version_signal(gp_skeleton);
    pwl_core_emit_notice_module_recovery_finish(gp_skeleton, PCIE_UPDATE_BASE_FLZ);
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
    FILE *fp = NULL;
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
    return 0;
}

static void bus_acquired_hdl(GDBusConnection *connection,
                             const gchar     *bus_name,
                             gpointer         user_data) {
    g_autoptr(GError) pError = NULL;

    /** Second step: Try to get a connection to the given bus. */
    gp_skeleton = pwl_core_skeleton_new();

    /** Third step: Attach to dbus signals. */
    (void) g_signal_connect(gp_skeleton, "handle-madpt-ready-method", G_CALLBACK(madpt_ready_method), NULL);
    (void) g_signal_connect(gp_skeleton, "handle-request-update-fw-version-method", G_CALLBACK(request_update_fw_version), NULL);
    (void) g_signal_connect(gp_skeleton, "handle-request-fw-update-check-method", G_CALLBACK(request_fw_update_check), NULL);
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

static void subscriber_ready_status_update(MbimDevice *dev, MbimMessage *message) {
    MbimSubscriberReadyState ready_state;
    gboolean success = FALSE;

    if (mbim_device_check_ms_mbimex_version(dev, 3, 0)) {
        success = mbim_message_ms_basic_connect_v3_subscriber_ready_status_notification_parse(
                  message, &ready_state, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        if (!success) return;
    } else {
        success = mbim_message_subscriber_ready_status_notification_parse(
                  message, &ready_state, NULL, NULL, NULL, NULL, NULL, NULL);
        if (!success) return;
    }

    pwl_sim_state_t state = PWL_SIM_STATE_UNKNOWN;

    switch (ready_state) {
    case MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED:
        PWL_LOG_DEBUG("MBIM_SUBSCRIBER_READY_STATE_NOT_INITIALIZED");
        break;
    case MBIM_SUBSCRIBER_READY_STATE_INITIALIZED:
        state = PWL_SIM_STATE_INITIALIZED;
        PWL_LOG_DEBUG("MBIM_SUBSCRIBER_READY_STATE_INITIALIZED");
        break;
    case MBIM_SUBSCRIBER_READY_STATE_SIM_NOT_INSERTED:
        state = PWL_SIM_STATE_NOT_INSERTED;
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

    pwl_core_emit_subscriber_ready_state_change(gp_skeleton, state);
}

static void register_state_update(MbimDevice *dev, MbimMessage *message) {
    MbimRegisterState register_state = MBIM_REGISTER_STATE_UNKNOWN;
    g_autofree gchar *provider_id = NULL;
    g_autofree gchar *provider_name = NULL;
    gboolean success = FALSE;

    gboolean status_indicate_b = (mbim_message_get_message_type(message) == MBIM_MESSAGE_TYPE_INDICATE_STATUS);

    if (mbim_device_check_ms_mbimex_version(dev, 2, 0)) {
        if (status_indicate_b) {
            success = mbim_message_ms_basic_connect_v2_register_state_notification_parse(
                      message, NULL, &register_state, NULL, NULL, NULL, &provider_id,
                      &provider_name, NULL, NULL, NULL, NULL);
            if (!success) return;
        } else {
            success = mbim_message_ms_basic_connect_v2_register_state_response_parse(
                      message, NULL, &register_state, NULL, NULL,  NULL,  &provider_id,
                      &provider_name, NULL, NULL, NULL, NULL);
            if (!success) return;
        }
    } else {
        if (status_indicate_b) {
            success = mbim_message_register_state_notification_parse(message,
                      NULL,  &register_state, NULL, NULL, NULL, &provider_id,
                      &provider_name, NULL, NULL, NULL);
            if (!success) return;
        } else {
            success = mbim_message_register_state_response_parse(message, NULL,
                      &register_state, NULL, NULL, NULL, &provider_id,
                      &provider_name, NULL, NULL, NULL);
            if (!success) return;
        }
    }

    if (DEBUG) PWL_LOG_DEBUG("register state update %s %s %s",
                              mbim_register_state_get_string(register_state),
                              provider_id, provider_name);
}

static void basic_connect_cb(MbimDevice *dev, MbimMessage *message) {
    switch (mbim_message_indicate_status_get_cid(message)) {
        case MBIM_CID_BASIC_CONNECT_SIGNAL_STATE:
            if (DEBUG) PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_SIGNAL_STATE");
            break;
        case MBIM_CID_BASIC_CONNECT_REGISTER_STATE:
            if (DEBUG) PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_REGISTER_STATE");
            if (DEBUG) register_state_update(dev, message);
            break;
        case MBIM_CID_BASIC_CONNECT_CONNECT:
            if (DEBUG) PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_CONNECT");
            break;
        case MBIM_CID_BASIC_CONNECT_SUBSCRIBER_READY_STATUS:
            if (DEBUG) PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_SUBSCRIBER_READY_STATUS");
            subscriber_ready_status_update(dev, message);
            break;
        case MBIM_CID_BASIC_CONNECT_PACKET_SERVICE:
            if (DEBUG) PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_PACKET_SERVICE");
            break;
        case MBIM_CID_BASIC_CONNECT_PROVISIONED_CONTEXTS:
            if (DEBUG) PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_PROVISIONED_CONTEXTS");
        case MBIM_CID_BASIC_CONNECT_IP_CONFIGURATION:
            if (DEBUG) PWL_LOG_DEBUG("MBIM_CID_BASIC_CONNECT_IP_CONFIGURATION");
        default:
            if (DEBUG) PWL_LOG_ERR("basic connect indicate cid unknown");
            break;
    }
}

static void mbim_device_close_cb(MbimDevice *dev, GAsyncResult *res) {
    PWL_LOG_INFO("MBIM Device close cb");
    GError *error = NULL;

    if (!mbim_device_close_finish(dev, res, &error))
        g_error_free(error);
}

static void device_close() {
    PWL_LOG_INFO("MBIM Device close");

    mbim_device_close(g_device, PWL_CLOSE_MBIM_TIMEOUT_SEC, NULL,
                     (GAsyncReadyCallback) mbim_device_close_cb, NULL);

    g_clear_object(&g_device);
}

static void mbim_device_error_cb(MbimDevice *dev, GError *error) {
    if (g_error_matches(error, MBIM_PROTOCOL_ERROR, MBIM_PROTOCOL_ERROR_NOT_OPENED)) {
        PWL_LOG_ERR("device error %s", mbim_device_get_path(dev));
        mbim_device_close_force(dev, NULL);

        if (g_cancellable)
            g_object_unref(g_cancellable);
        if (g_device)
            g_object_unref(g_device);

        g_cancellable = NULL;
        g_device = NULL;

        // retry
        GThread *mbim_thread = g_thread_new("mbim_thread", mbim_device_thread, NULL);
    }
}

static void mbim_device_removed_cb(MbimDevice *dev) {
    PWL_LOG_ERR("remove device %s", mbim_device_get_path(dev));

    g_signal_handlers_disconnect_by_func(dev, mbim_device_error_cb, NULL);
    g_signal_handlers_disconnect_by_func(dev, mbim_device_removed_cb, NULL);
    pwl_core_emit_subscriber_ready_state_change(gp_skeleton, PWL_SIM_STATE_NOT_INSERTED);
    device_close();

    if (g_cancellable)
        g_object_unref(g_cancellable);
    if (g_device)
        g_object_unref(g_device);

    g_cancellable = NULL;
    g_device = NULL;

    // retry
    GThread *mbim_thread = g_thread_new("mbim_thread", mbim_device_thread, NULL);
}


static void mbim_indication_cb(MbimDevice *self, MbimMessage *message) {
    MbimService service = mbim_message_indicate_status_get_service(message);

    if (DEBUG) PWL_LOG_DEBUG("received service %s, command %s",
                              mbim_service_get_string(service),
                              mbim_cid_get_printable(service,
                              mbim_message_indicate_status_get_cid(message)));

    if (service == MBIM_SERVICE_BASIC_CONNECT) {
        basic_connect_cb(self, message);
    }
}

static void device_open_cb(MbimDevice *dev, GAsyncResult *res) {
    g_autoptr(GError) error = NULL;

    if (!mbim_device_open_finish(dev, res, &error)) {
        PWL_LOG_ERR("Couldn't open Mbim Device: %s\n", error->message);

        // retry
        GThread *mbim_thread = g_thread_new("mbim_thread", mbim_device_thread, NULL);
        return;
    }

    PWL_LOG_DEBUG("MBIM Device %s opened.", mbim_device_get_path_display(dev));

    g_signal_connect(g_device, MBIM_DEVICE_SIGNAL_REMOVED,
                     G_CALLBACK(mbim_device_removed_cb), NULL);

    g_signal_connect(g_device, MBIM_DEVICE_SIGNAL_ERROR,
                     G_CALLBACK(mbim_device_error_cb), NULL);

    g_signal_connect(g_device, MBIM_DEVICE_SIGNAL_INDICATE_STATUS,
                     G_CALLBACK(mbim_indication_cb), NULL);
}


static void device_new_cb(GObject *unused, GAsyncResult *res) {
    g_autoptr(GError) error = NULL;

    g_device = mbim_device_new_finish(res, &error);
    if (!g_device) {
        PWL_LOG_ERR("Couldn't create MbimDevice object: %s\n", error->message);

        // retry
        GThread *mbim_thread = g_thread_new("mbim_thread", mbim_device_thread, NULL);
        return;
    }

    mbim_device_open_full(g_device, MBIM_DEVICE_OPEN_FLAGS_PROXY,
                          PWL_OPEN_MBIM_TIMEOUT_SEC, g_cancellable,
                          (GAsyncReadyCallback) device_open_cb, NULL);
}

gboolean mbim_dbus_init(void) {
    g_autoptr(GFile) file = NULL;

    gchar port[20];
    memset(port, 0, sizeof(port));
    if (!pwl_find_mbim_port(port, sizeof(port))) {
        if (DEBUG) PWL_LOG_ERR("find mbim port fail at gdbus init!");
        return FALSE;
    }

    file = g_file_new_for_path(port);
    g_cancellable = g_cancellable_new();

    mbim_device_new(file, g_cancellable, (GAsyncReadyCallback) device_new_cb, NULL);

    return TRUE;
}

static gpointer mbim_device_thread(gpointer data) {
    while (!mbim_dbus_init()) {
        sleep(5);
    }
    PWL_LOG_INFO("mbim device open start");
    return ((void*)0);
}

static gpointer mbim_monitor_thread_func(gpointer data) {
    //TODO: Check better way to check device ready
    PWL_LOG_DEBUG("Sleep 60s wait to device ");
    sleep(60);

    gchar port [20];
    memset(port, 0, sizeof(port));

    while (TRUE) {
        if (find_mbim_port(port, sizeof(port)) == RET_OK) {
            // Found MBIM port, try to get module cap info
            if (check_module_info_v2() == RET_OK) {
                // Check module ok, clear bootup failure count to 0 than idle
                g_bootup_failure_count = 0;
                set_bootup_status_value(BOOTUP_FAILURE_COUNT, g_bootup_failure_count);
                PWL_LOG_DEBUG("Check module pass!");
                PWL_LOG_DEBUG("Notify fwupdate start extract flz and check for update");

                // Notice pref to update fw version
                PWL_LOG_INFO("Notify pref to update fw version");
                pwl_core_emit_get_fw_version_signal(gp_skeleton);
                pwl_core_emit_notice_module_recovery_finish(gp_skeleton, PCIE_UPDATE_BASE_FLZ);
                break;
            }
        }
        // Check if module in abnormal state
        memset(port, 0, sizeof(port));
        if (find_abnormal_port(port, sizeof(port)) == RET_OK) {
            // Module in abnormal state, check if hw reset
            g_bootup_failure_count++;
            set_bootup_status_value(BOOTUP_FAILURE_COUNT, g_bootup_failure_count);
            if (g_bootup_failure_count <= g_bootup_failure_limit) {
                // Bootup Failure < Max failure, do hw reset
                PWL_LOG_DEBUG("Do flash recovery check");

                // Check if flash data folder exist
                if (access(UPDATE_FW_FOLDER_FILE, F_OK) == 0 ||
                    access(UPDATE_DEV_FOLDER_FILE, F_OK) == 0) {
                    // Notice pref to update fw version
                    PWL_LOG_INFO("Notify pref to update fw version");
                    pwl_core_emit_get_fw_version_signal(gp_skeleton);
                    pwl_core_emit_notice_module_recovery_finish(gp_skeleton, PCIE_UPDATE_BASE_FLASH_FOLDER);
                } else {
                    PWL_LOG_DEBUG("Flash data folder not exist, in to idle");
                }
                break;
            } else {
                // Bootup Failure >= Max failure, into idle
                PWL_LOG_DEBUG("Bootup Failure count reach max count, in to idle");
                break;
            }
        } else {
            // Can't found mbim and abnormal port, do re-scan
            g_rescan_failure_count++;
            PWL_LOG_DEBUG("Rescan failure count: %d", g_rescan_failure_count);
            if (g_rescan_failure_count <= MAX_RESCAN_FAILURE) {
                PWL_LOG_DEBUG("Do re-scan");
                do_pci_hw_reset(DEVICE_HW_RESCAN);
            } else {
                // Rescan failure >= Max Failure, do hw reset
                PWL_LOG_DEBUG("Rescan count reach max limit, do flash recovery check.");
                // Check if flash data folder exist
                if (access(UPDATE_FW_FOLDER_FILE, F_OK) == 0 ||
                    access(UPDATE_DEV_FOLDER_FILE, F_OK) == 0) {
                    // Notice pref to update fw version
                    PWL_LOG_INFO("Notify pref to update fw version");
                    pwl_core_emit_get_fw_version_signal(gp_skeleton);
                    pwl_core_emit_notice_module_recovery_finish(gp_skeleton, PCIE_UPDATE_BASE_FLASH_FOLDER);
                } else {
                    PWL_LOG_DEBUG("Flash data folder not exist, in to idle");
                }
                break;
            }
        }
    }

    return ((void*)0);
}

static void prepare_for_sleep_handler(GDBusProxy *proxy, const gchar *sendername,
                                      const gchar *signalname, GVariant *args,
                                      gpointer data) {
    if (strcmp(signalname, "PrepareForSleep") == 0) {
        gboolean suspend;
        g_variant_get(args, "(b)", &suspend);

        if (suspend) {
            PWL_LOG_INFO("Host system about to suspend");
        } else {
            PWL_LOG_INFO("Host system resuming");
        }
    }
}

void suspend_monitor_init() {
    GDBusConnection *connection;
    GDBusProxy *proxy;
    g_autoptr(GError) error = NULL;

    connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (connection == NULL) {
        PWL_LOG_ERR("Failed to get system bus connection: %s", error->message);
        return;
    }

    proxy = g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, NULL,
                                  "org.freedesktop.login1",
                                  "/org/freedesktop/login1",
                                  "org.freedesktop.login1.Manager",
                                  NULL, &error);

    if (proxy == NULL) {
        PWL_LOG_ERR("Failed to create proxy: %s", error->message);
        g_object_unref(connection);
        return;
    }

    g_signal_connect(proxy, "g-signal", G_CALLBACK(prepare_for_sleep_handler), NULL);
}

void update_autosuspend_delay() {
    gchar *command = "lspci -D -n | grep ";
    if (DEBUG) PWL_LOG_DEBUG("pcieid_info[0]: %s", pcieid_info[0]);

    gchar cmd[strlen(command) + strlen(pcieid_info[0]) + 1];
    memset(cmd, 0, sizeof(cmd));
    sprintf(cmd, "%s%s", command, pcieid_info[0]);

    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("device id check cmd error!!!");
    }

    char response[200];
    memset(response, 0, sizeof(response));
    char *ret = fgets(response, sizeof(response), fp);

    pclose(fp);

    if (ret != NULL && strlen(response) > 0) {
        const char s[2] = " ";
        char *domain;
        domain = strtok(response, s);
        if (!domain) return;

        char node_path[strlen(AUTOSUSPEND_DELAY_NODE_PATH) + 20];
        memset(node_path, 0, sizeof(node_path));
        sprintf(node_path, AUTOSUSPEND_DELAY_NODE_PATH, domain);
        if (DEBUG) PWL_LOG_DEBUG("auto suspend node path: %s", node_path);

        FILE *fp = fopen(node_path, "w");
        if (fp) {
            fprintf(fp, "%s", AUTOSUSPEND_DELAY_VALUE);
            fclose(fp);
        } else {
            PWL_LOG_ERR("Auto suspend delay node open fail!");
        }
    }
}

gint main() {
    PWL_LOG_INFO("start");

    GThread *mbim_thread = g_thread_new("mbim_thread", mbim_device_thread, NULL);

    gint owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM, PWL_GDBUS_NAME,
                                   G_BUS_NAME_OWNER_FLAGS_NONE, bus_acquired_hdl,
                                   name_acquired_hdl, name_lost_hdl, NULL, NULL);

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

    pwl_device_type_t type = pwl_get_device_type_await();
    if (type == PWL_DEVICE_TYPE_USB) {
        gpio_init();
    } else if (type == PWL_DEVICE_TYPE_PCIE) {
        // system suspend/resume state monitor
        suspend_monitor_init();
        update_autosuspend_delay();

        // Get bootup failure limit from file
        if (read_config_from_file(BOOTUP_CONFIG_FILE, CONFIG_MAX_BOOTUP_FAILURE,
                                 &g_bootup_failure_limit) == RET_OK) {
            PWL_LOG_DEBUG("Max bootup failure limit: %d", g_bootup_failure_limit);
        } else {
            g_bootup_failure_limit = MAX_BOOTUP_FAILURE;
        }
        // Get bootup failure count from file
        if (bootup_status_init() == 0) {
            get_bootup_status_value(BOOTUP_FAILURE_COUNT, &g_bootup_failure_count);
        } else {
            g_bootup_failure_count = 0;
        }
        PWL_LOG_DEBUG("Type is PWL_DEVICE_TYPE_PCIE, start monitor thread");
        GThread *mbim_monitor_thread = g_thread_new("mbim_monitor_thread", mbim_monitor_thread_func, NULL);
    }

    gp_loop = g_main_loop_new(NULL, FALSE);

    g_main_loop_run(gp_loop);

    pci_mbim_device_deinit();
    if (g_cancellable)
        g_object_unref(g_cancellable);
    if (g_device)
        g_object_unref(g_device);

    // de-init
    if (0 != gp_loop) {
        g_main_loop_quit(gp_loop);
        g_main_loop_unref(gp_loop);
    }

    return 0;
}
