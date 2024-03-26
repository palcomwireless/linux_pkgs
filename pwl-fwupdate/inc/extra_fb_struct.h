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

#define FB_MAX_INFO_COUNT   16
#define FB_MAX_MSG_LEN     256
#define SERIAL_NUMBER_LEN   15

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct FASTBOOT_DATA
{
    char g_device_serial_number[SERIAL_NUMBER_LEN];
    char gfb_error_msg[FB_MAX_MSG_LEN];
    char gfb_info_msg[FB_MAX_INFO_COUNT][FB_MAX_MSG_LEN];
    int g_fasboot_output_msg_count;
    int g_fasboot_output_msg_count_keep;
} fastboot_data_t;

#ifdef	__cplusplus
}
#endif

