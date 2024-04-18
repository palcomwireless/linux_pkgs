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

#include <glib.h>

#include "log.h"
#include "CoreGdbusGenerated.h"

#define COMPARE_FW_IMAGE_VERSION 1

#define IMAGE_MONITOR_PATH          "/opt/pwl/"
#define IMAGE_FW_FOLDER_PATH        "/opt/pwl/firmware/fw/"
#define IMAGE_CARRIER_FOLDER_PATH   "/opt/pwl/firmware/carrier_pri/"
#define IMAGE_OEM_FOLDER_PATH       "/opt/pwl/firmware/oem_pri/"
#define UPDATE_UNZIP_PATH           "/opt/pwl/firmware/"
#define UPDATE_FW_ZIP_FILE          "/opt/pwl/firmware/FwPackage.zip"
#define MONITOR_FOLDER_NAME         "firmware"

#define ENABLE_INSTALLED_FASTBOOT_CHECK 0
#define CHECK_FASTBOOT_TH               60
#define FASTBOOT_EARSE_COMMAND     0
#define FASTBOOT_REBOOT_COMMAND    1
#define FASTBOOT_FLASH_COMMAND     2
#define FASTBOOT_OEM_COMMAND       3
#define FASTBOOT_FLASHING_COMMAND  4

#define RET_SIGNAL_HANDLE_SIZE 4

#define GET_TEST_SKU_ID        0

//GPIO Reset 
#define ENABLE_GPIO_RESET               1
#define STATUS_LINE_LENGTH              128
#define FW_UPDATE_STATUS_RECORD         "/opt/pwl/fw_update_status"
#define FIND_FASTBOOT_RETRY_COUNT       "Find_fastboot_retry_count"
#define WAIT_MODEM_PORT_RETRY_COUNT     "Wait_modem_port_retry_count"
#define WAIT_AT_PORT_RETRY_COUNT        "Wait_at_port_retry_count"
#define FW_UPDATE_RETRY_COUNT           "Fw_update_retry_count"

#define FW_UPDATE_RETRY_TH              5
#define FIND_FASTBOOT_RETRY_TH          10
#define WAIT_MODEM_PORT_RETRY_TH        10
#define WAIT_AT_PORT_RETRY_TH           10

int start_udpate_process();
gint set_preferred_carrier();
gint del_tune_code();
gint set_oem_pri_version();
gint get_ati_info();
int fw_update_status_init();
int set_fw_update_status_value(char *key, int value);
int get_fw_update_status_value(char *key, int *result);
int count_int_length(unsigned x);
char *get_test_sku_id();
#endif
