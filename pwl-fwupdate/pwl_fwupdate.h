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

typedef struct {
    signal_get_retry_fw_update_callback callback_retry_fw_update;
} signal_callback_t;

int start_update_process(gboolean is_startup);
gint set_preferred_carrier();
gint del_tune_code();
gint set_oem_pri_version();
gint get_ati_info();
char *get_test_sku_id();
void signal_callback_retry_fw_update(const gchar* arg);
void registerSignalCallback(signal_callback_t *callback);

#endif
