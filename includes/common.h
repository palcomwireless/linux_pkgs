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

#ifndef __COMMON_H__
#define __COMMON_H__

#include <glib.h>
#include <mqueue.h>
#include <stdint.h>
#include <unistd.h>

#define MFR_NAME                        "Dell"

#define PWL_CMD_TIMEOUT_SEC             5
#define PWL_OPEN_MBIM_TIMEOUT_SEC       30
#define PWL_CLOSE_MBIM_TIMEOUT_SEC      5
#define PWL_MBIM_READY_SEC              15

#define INFO_BUFFER_SIZE                100
#define PWL_MAX_MFR_SIZE                10 // min size for "Dell Inc."
#define PWL_MAX_SKUID_SIZE              15

#define STATUS_LINE_LENGTH              128
#define FW_UPDATE_STATUS_RECORD         "/opt/pwl/firmware/fw_update_status"
// #define HAS_BEEN_FW_UPDATE_FLAG         "/opt/pwl/has_been_fw_update"
#define BOOTUP_STATUS_RECORD            "/opt/pwl/bootup_status"
#define FIND_FASTBOOT_RETRY_COUNT       "Find_fastboot_retry_count"
#define WAIT_MODEM_PORT_RETRY_COUNT     "Wait_modem_port_retry_count"
#define WAIT_AT_PORT_RETRY_COUNT        "Wait_at_port_retry_count"
#define FW_UPDATE_RETRY_COUNT           "Fw_update_retry_count"
#define DO_HW_RESET_COUNT               "Do_hw_reset_count"
#define NEED_RETRY_FW_UPDATE            "Need_retry_fw_update"
#define JP_FCC_CONFIG_COUNT             "jp_fcc_config_count"
#define BOOTUP_FAILURE_COUNT            "Bootup_failure_count"

#define DEVICE_PACKAGE_VERSION_LENGTH   15
#define FW_UPDATE_RETRY_TH              1
#define FIND_FASTBOOT_RETRY_TH          10
#define WAIT_MODEM_PORT_RETRY_TH        10
#define WAIT_AT_PORT_RETRY_TH           10
#define HW_RESET_RETRY_TH               5
#define JP_FCC_CONFIG_RETRY_TH          3
#define MAX_BOOTUP_FAILURE              1
#define MAX_RESCAN_FAILURE              3

#define PWL_MQ_MAX_MSG                  10
#define PWL_MQ_MAX_CONTENT_LEN          30
#define PWL_MQ_MAX_RESP                 500
#define PWL_MQ_ID_INVALID               0
#define PWL_MQ_ID_CORE                  1
#define PWL_MQ_ID_MADPT                 2
#define PWL_MQ_ID_PREF                  3
#define PWL_MQ_ID_FWUPDATE              4
#define PWL_MQ_ID_UNLOCK                5

#define PWL_MQ_PATH_INVALID             "/pwl"
#define PWL_MQ_PATH_CORE                "/pwl_core"
#define PWL_MQ_PATH_MADPT               "/pwl_madpt"
#define PWL_MQ_PATH_PREF                "/pwl_pref"
#define PWL_MQ_PATH_FWUPDATE            "/pwl_fwupdate"
#define PWL_MQ_PATH_UNLOCK              "/pwl_unlock"

#define PWL_UNKNOWN_SIM_CARRIER         "UNKNOWN"
#define PWL_FW_UPDATE_RETRY_LIMIT       3
#define PWL_OEM_PRI_RESET_RETRY         3

// For pcie device update
#define UPDATE_FW_FLZ_FILE              "/opt/pwl/firmware/FwPackage.flz"
#define UPDATE_DEV_FLZ_FILE             "/opt/pwl/firmware/DevPackage.flz"
#define UPDATE_FW_FOLDER_FILE           "/opt/pwl/firmware/FwPackage"
#define UPDATE_DEV_FOLDER_FILE          "/opt/pwl/firmware/DevPackage"
#define MAX_FW_PACKAGE_PATH_LEN         128
#define TYPE_FLASH_FLZ                  0
#define TYPE_FLASH_FOLDER               1
#define PCIE_UPDATE_BASE_FLZ            0
#define PCIE_UPDATE_BASE_FLASH_FOLDER   1

#define PWL_MQ_PATH(x) \
    ((x == PWL_MQ_ID_CORE)        ? PWL_MQ_PATH_CORE : \
    (x == PWL_MQ_ID_MADPT)        ? PWL_MQ_PATH_MADPT : \
    (x == PWL_MQ_ID_PREF)         ? PWL_MQ_PATH_PREF : \
    (x == PWL_MQ_ID_FWUPDATE)     ? PWL_MQ_PATH_FWUPDATE : \
    (x == PWL_MQ_ID_UNLOCK)       ? PWL_MQ_PATH_UNLOCK : PWL_MQ_PATH_INVALID)


#define CID_DESTINATION(x) \
    ((x >= 0                && x < PLW_CID_MAX_PREF)         ? PWL_MQ_PATH_PREF : \
    (x >= PLW_CID_MAX_PREF  && x < PLW_CID_MAX_MADPT)        ? PWL_MQ_PATH_MADPT : PWL_MQ_PATH_INVALID)


typedef enum {
    /* request to pref */
    PWL_CID_GET_MFR,
    PWL_CID_GET_SKUID,
    PWL_CID_GET_MAIN_FW_VER,
    PWL_CID_GET_AP_VER,
    PWL_CID_GET_CARRIER_VER,
    PWL_CID_GET_OEM_VER,
    PWL_CID_UPDATE_FW_VER,
    PWL_CID_GET_SIM_CARRIER,
    PWL_CID_GET_MD_VER,
    PWL_CID_GET_OP_VER,
    PWL_CID_GET_DPV_VER,
    PLW_CID_MAX_PREF,
    /* request to madpt */
    PWL_CID_GET_CIMI,
    PWL_CID_GET_ATE,
    PWL_CID_GET_ATI,
    PWL_CID_GET_FW_VER,
    PWL_CID_SWITCH_TO_FASTBOOT,
    PWL_CID_CHECK_OEM_PRI_VERSION,
    PWL_CID_GET_PREF_CARRIER,
    PWL_CID_SET_PREF_CARRIER,
    PWL_CID_SET_OEM_PRI_VERSION,
    PWL_CID_DEL_TUNE_CODE,
    PWL_CID_GET_CRSM,
    PWL_CID_AT_UCOMP,
    PWL_CID_RESET,
    PWL_CID_GET_OEM_PRI_INFO,
    PWL_CID_GET_JP_FCC_AUTO_REBOOT,
    PWL_CID_ENABLE_JP_FCC_AUTO_REBOOT,
    PWL_CID_GET_OEM_PRI_RESET,
    PWL_CID_GET_PCIE_DEVICE_VERSION,
    PWL_CID_GET_PCIE_AP_VERSION,
    PWL_CID_GET_PCIE_OP_VERSION,
    PWL_CID_GET_PCIE_OEM_VERSION,
    PWL_CID_GET_PCIE_DPV_VERSION,
    PWL_CID_GET_CARRIER_ID,
    PWL_CID_MADPT_RESTART,
    PLW_CID_MAX_MADPT,
    PWL_CID_MAX
} pwl_cid_t;

static const gchar * const cid_name[] = {
    [PWL_CID_GET_MFR] = "GET_MFR",
    [PWL_CID_GET_SKUID] = "GET_SKUID",
    [PWL_CID_GET_MAIN_FW_VER] = "GET_MAIN_FW_VER",
    [PWL_CID_GET_AP_VER] = "GET_AP_VER",
    [PWL_CID_GET_CARRIER_VER] = "GET_CARRIER_VER",
    [PWL_CID_GET_OEM_VER] = "GET_OEM_VER",
    [PWL_CID_UPDATE_FW_VER] = "UPDATE_FW_VER",
    [PWL_CID_GET_SIM_CARRIER] = "GET_SIM_CARRIER",
    [PWL_CID_GET_MD_VER] = "GET_MD_VER",
    [PWL_CID_GET_OP_VER] = "GET_OP_VER",
    [PWL_CID_GET_DPV_VER] = "GET_DPV_VER",
    [PWL_CID_GET_CIMI] = "GET_CIMI",
    [PWL_CID_GET_ATE] = "GET_ATE",
    [PWL_CID_GET_ATI] = "GET_ATI",
    [PWL_CID_GET_FW_VER] = "GET_FW_VER",
    [PWL_CID_SWITCH_TO_FASTBOOT] = "SWITCH_TO_FASTBOOT",
    [PWL_CID_CHECK_OEM_PRI_VERSION] = "CHECK_OEM_PRI_VERSION",
    [PWL_CID_GET_PREF_CARRIER] = "GET_PREF_CARRIER",
    [PWL_CID_SET_PREF_CARRIER] = "SET_PREF_CARRIER",
    [PWL_CID_SET_OEM_PRI_VERSION] = "SET_OEM_PRI_VERSION",
    [PWL_CID_DEL_TUNE_CODE] = "DEL_TUNE_CODE",
    [PWL_CID_GET_CRSM] = "GET_CRSM",
    [PWL_CID_AT_UCOMP] = "AT_UCOMP",
    [PWL_CID_RESET] = "RESET",
    [PWL_CID_GET_OEM_PRI_INFO] = "GET_OEM_PRI_INFO",
    [PWL_CID_GET_JP_FCC_AUTO_REBOOT] = "GET_JP_FCC_AUTO_REBOOT",
    [PWL_CID_ENABLE_JP_FCC_AUTO_REBOOT] = "ENABLE_JP_FCC_AUTO_REBOOT",
    [PWL_CID_GET_OEM_PRI_RESET] = "GET_OEM_PRI_RESET",
    [PWL_CID_GET_PCIE_DEVICE_VERSION] = "GET_PCIE_DEVICE_VERSION",
    [PWL_CID_GET_PCIE_AP_VERSION] = "GET_PCIE_AP_VERSION",
    [PWL_CID_GET_PCIE_OP_VERSION] = "GET_PCIE_OP_VERSION",
    [PWL_CID_GET_PCIE_OEM_VERSION] = "GET_PCIE_OEM_VERSION",
    [PWL_CID_GET_PCIE_DPV_VERSION] = "GET_PCIE_DPV_VERSION",
    [PWL_CID_GET_CARRIER_ID] = "GET_CARRIER_ID",
    [PWL_CID_MADPT_RESTART] = "MADPT_RESTART",
};

typedef enum {
    PWL_CID_STATUS_NONE,
    PWL_CID_STATUS_OK,
    PWL_CID_STATUS_ERROR,
    PWL_CID_STATUS_TIMEOUT,
    PWL_CID_STATUS_BUSY
} pwl_cid_status_t;

static const gchar * const cid_status_name[] = {
    [PWL_CID_STATUS_OK] = "OK",
    [PWL_CID_STATUS_ERROR] = "ERROR",
    [PWL_CID_STATUS_TIMEOUT] = "TIMEOUT",
    [PWL_CID_STATUS_BUSY] = "BUSY"
};

typedef struct {
    uint32_t            sender_id;
    uint32_t            pwl_cid;
    pwl_cid_status_t    status;
    char                response[PWL_MQ_MAX_RESP];
    char                content[PWL_MQ_MAX_CONTENT_LEN];
} msg_buffer_t;

typedef enum {
    PWL_AT_INTF_NONE,
    PWL_AT_OVER_MBIM_CONTROL_MSG,
    PWL_AT_OVER_MBIM_CLI,
    PWL_AT_OVER_MBIM_API,
    PWL_AT_CHANNEL
} pwl_at_intf_t;

typedef enum {
    PWL_CID_GET_ENABLE_STATE_DISABLED,
    PWL_CID_GET_ENABLE_STATE_ENABLED,
    PWL_CID_GET_ENABLE_STATE_ERROR
} pwl_get_enable_state_t;

typedef enum {
    PWL_SIM_STATE_UNKNOWN,
    PWL_SIM_STATE_NOT_INSERTED,
    PWL_SIM_STATE_INITIALIZED
} pwl_sim_state_t;

typedef enum {
    PWL_DEVICE_TYPE_UNKNOWN,
    PWL_DEVICE_TYPE_USB,
    PWL_DEVICE_TYPE_PCIE
} pwl_device_type_t;

enum RETURNS {
    RET_FAILED = -1,
    RET_OK = 0
};

gboolean pwl_discard_old_messages(const gchar *path);
gboolean get_host_info(const gchar *cmd, gchar *buff, gint buff_len);
gboolean filter_host_info_header(const gchar *header, gchar *info, gchar *buff, gint buff_len);
void pwl_get_manufacturer(gchar *buff, gint buff_len);
void pwl_get_skuid(gchar *buff, gint buff_len);
gboolean pwl_module_usb_id_exist(gchar *usbid);
gboolean pwl_module_pcie_id_exist(gchar *pcieid);
pwl_device_type_t pwl_get_device_type();
pwl_device_type_t pwl_get_device_type_await();
gboolean cond_wait(pthread_mutex_t *mutex, pthread_cond_t *cond, gint wait_time);
void send_message_reply(uint32_t cid, uint32_t sender_id, uint32_t dest_id, pwl_cid_status_t status, char *msg);
void print_message_info(msg_buffer_t* message);
gboolean pwl_find_mbim_port(gchar *port_buff_ptr, guint32 port_buff_size);
gboolean pwl_set_command(const gchar *command, gchar **response);
gboolean pwl_set_command_available();
int fw_update_status_init();
int set_fw_update_status_value(char *key, int value);
int get_fw_update_status_value(char *key, int *result);
int bootup_status_init();
int set_bootup_status_value(char *key, int value);
int get_bootup_status_value(char *key, int *result);
int count_int_length(unsigned x);
int read_config_from_file(char *file_name, char*key, int *value);
int get_fwupdate_subsysid(char *subsysid);
int remove_folder(char *path);

#endif
