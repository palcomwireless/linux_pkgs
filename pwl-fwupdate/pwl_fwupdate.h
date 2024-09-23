/*
 * PWL Linux service packages
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

/*
 * Copyright (C) 2024 Palcom International Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __PWL_FWUPDATE_H__
#define __PWL_FWUPDATE_H__

// #include <glib.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/tree.h>

#include "log.h"
#include "CoreGdbusGenerated.h"

// 0: Not check, allow any version to download
// 1: Allow upgrade or downgrade, but ignore the same version of image.
// 2: Only allow upgrade
#define COMPARE_FW_IMAGE_VERSION 1

#define IMAGE_MONITOR_PATH          "/opt/pwl/"
#define IMAGE_FW_FOLDER_PATH        "/opt/pwl/firmware/fw/"
#define IMAGE_CARRIER_FOLDER_PATH   "/opt/pwl/firmware/carrier_pri/"
#define IMAGE_OEM_FOLDER_PATH       "/opt/pwl/firmware/oem_pri/"
#define UPDATE_UNZIP_PATH           "/opt/pwl/firmware/"
#define UPDATE_FW_ZIP_FILE          "/opt/pwl/firmware/FwPackage.zip"
#define MONITOR_FOLDER_NAME         "firmware"

#define CLOSE_TYPE_ERROR            1
#define CLOSE_TYPE_SKIP             2
#define CLOSE_TYPE_SUCCESS          3
#define CLOSE_TYPE_RETRY            4


// For pcie device
#define CHECK_AP_VERSION            1
#define CHECK_MD_VERSION            1
#define CHECK_OP_VERSION            1
#define CHECK_OEM_VERSION           1
#define CHECK_DPV_VERSION           1
#define CHECK_CHECKSUM              1

#define FASTBOOT_CMD_TIMEOUT_SEC    10
#define MAX_DONWLOAD_IMAGES         50
#define MAX_IMG_FILE_NAME_LEN       128
#define MAX_COMMAND_LEN             256
#define MAX_CHECKSUM_LEN            16
#define INDEX_PARTITION             1
#define INDEX_IMAGE                 2
#define INDEX_CHECKSUM              3
#define UPDATE_TYPE_FULL            1
#define UPDATE_TYPE_ONLY_FW         2
#define UPDATE_TYPE_ONLY_DEV        3
#define GETVER_RETRY_LIMIT          50
#define FB_CMD_CONTINUE_SUCCESS_TH  10

#define UNZIP_FOLDER_FW             "/opt/pwl/firmware/FwPackage"
#define UNZIP_FOLDER_DVP            "/opt/pwl/firmware/DevPackage"
#define FLASH_TABLE_FILE_NAME       "/opt/pwl/firmware/flash_table.txt"
#define SCATTER_PATH                "/opt/pwl/firmware/FwPackage/scatter.xml"
#define FW_PACKAGE_CHECKSUM_PATH    "/opt/pwl/firmware/FwPackage/checksum.xml"
#define DPV_CHECKSUM_PATH           "/opt/pwl/firmware/DevPackage/checksum.xml"
#define T7XX_MODE                   "t7xx_mode"
#define MODE_FASTBOOT_SWITCHING     "fastboot_switching"
#define MODE_HW_RESET               "reset"

#define NOT_IN_FW_UPDATE_PROCESSING     0
#define IN_FW_UPDATE_PROCESSING         1
// ===== 
#define ENABLE_INSTALLED_FASTBOOT_CHECK 0
#define CHECK_FASTBOOT_TH               60
#define FASTBOOT_EARSE_COMMAND     0
#define FASTBOOT_REBOOT_COMMAND    1
#define FASTBOOT_FLASH_COMMAND     2
#define FASTBOOT_OEM_COMMAND       3
#define FASTBOOT_FLASHING_COMMAND  4

#define RET_SIGNAL_HANDLE_SIZE 4

#define GET_TEST_SKU_ID        0
#define GET_TEST_SIM_CARRIER   0

//GPIO Reset 
#define ENABLE_GPIO_RESET               1

typedef void (*signal_get_retry_fw_update_callback)(const gchar*);
typedef void (*signal_notice_module_recovery_finish_callback)(int);

typedef struct {
    signal_get_retry_fw_update_callback callback_retry_fw_update;
    signal_notice_module_recovery_finish_callback callback_notice_module_recovery_finish;
} signal_callback_t;

int start_update_process(gboolean is_startup);
gint set_preferred_carrier();
gint del_tune_code();
gint set_oem_pri_version();
gint get_ati_info();
char *get_test_sku_id();
void signal_callback_retry_fw_update(const gchar* arg);
void registerSignalCallback(signal_callback_t *callback);
int get_oem_version_from_file(char *oem_file_name, char *oem_version);

// For pcie device
xmlXPathObjectPtr get_node_set (xmlDocPtr doc, xmlChar *xpath);
int unzip_flz(char *flz_file, char *unzip_folder);
int find_fw_download_image(char *subsysid, char *carrier_id, char *version);
int find_image_file_path(char *image_file_name, char *find_prefix, char *image_path);
int find_device_image(char *sku_id);
int generate_download_table(char *xml_file);
int parse_download_table_and_flash();
int check_image_downlaod_table();
int start_update_process_pcie(gboolean is_startup, int base_type);
int query_t7xx_mode(char *mode);
int find_fastboot_port(char *fastboot_port);
int switch_t7xx_mode(char *mode);
int send_fastboot_command(char *command, char *response);
int get_fastboot_resp(char *response);
int flash_image(char *partition, char *image_name, char *checksum);
int check_update_data(int check_type);
int parse_checksum(char *checksum_file, char *key_image, char *checksum_value);
int do_fastboot_reboot();
gint get_carrier_id();
void remove_flash_data(int type);

#endif
