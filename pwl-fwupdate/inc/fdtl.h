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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <poll.h>

#define MAX_PATH  260
#define MODEL_NAME_LEN  10
#define FASTBOOT_IGNORE          0
#define FASTBOOT_FRP_UNLOCK_KEY  1
#define FASTBOOT_UNLOCK          2
#define FASTBOOT_SOP_HDR         3
#define FASTBOOT_FLASH_FW        4
#define FASTBOOT_FLASH_OEM       5
#define FASTBOOT_FLASH_PRI       6
#define FASTBOOT_FLASH_PREF      7
#define MAX_OPTION_NUMBER  9

#define MAX_DOWNLOAD_FW_IMAGES    3
#define MAX_DOWNLOAD_PRI_IMAGES   100
#define MAX_DOWNLOAD_OEM_IMAGES   100
#define MAX_DOWNLOAD_FILES        (MAX_DOWNLOAD_FW_IMAGES + MAX_DOWNLOAD_PRI_IMAGES + MAX_DOWNLOAD_OEM_IMAGES)

#define CHECK_VERSION_FAILED  -1
#define SWITCH_TO_FASTBOOT_FAILED   -2
#define MODULE_VERSION_NOT_SUPPORT  -3
#define NOT_SUPPORT_MULTIPLE_DEVICES  -4
#define NO_FSN_MULTIPLE_DEVICES  -5

#define MAX_BUILD_ID_LEN 30

#define DOWNLOAD_PARAMETER_PARSING_FAILED  -1
#define MODEM_PORT_OPEN_FAILED -2
#define SWITCHING_TO_DOWNLOAD_MODE_FAILED -3
#define FASTBOOT_COMMAND_FAILED -4
#define FIREHOUSE_DOWNLOAD_FAILED -5 // not in use
#define FASTBOOT_FLASHING_FAILED -6
#define MODEM_PORT_OPENING_FAILS_AFTER_FIRMWARE_DOWNLOAD -8
#define MEMORY_ALLOCATION_FAILED -9
#define MODULE_FIRMWARE_VERSION_CHECK_FAILED -10
#define CHECK_MODEL_NAME_FAIL -11

#define SEND_ATE_COMMAND  1
#define CHECK_FW_VERSION  2
#define OEM_PRI_RESET     3
#define CHECK_OEM_PRI_VERSION  4
#define CHECK_MODEL_NAME  5

#define OEM_PRI_UPDATE_BOOTING  -1
#define OEM_PRI_UPDATE_START     0
#define OEM_PPI_UPDATE_INIT      1
#define OEM_PRI_UPDATE_RESET     2
#define OEM_PRI_UPDATE_NORESET   3
#define OEM_PRI_UPDATE_ATRESET   4
#define OEM_PRI_UPDATE_RESETPRI  5

typedef struct FDTL_DATA
{
    bool g_open_at_usb;
    char modem_port[MAX_PATH];
    char g_device_build_id[MAX_BUILD_ID_LEN];
    char g_device_serial_number[SERIAL_NUMBER_LEN];
    char g_device_model_name[MODEL_NAME_LEN];

    char g_first_temp_file_name[MAX_PATH];
    char g_image_temp_file_name[MAX_PATH];
    char g_pri_temp_file_name[MAX_PATH];

    char g_prefix_string[10];

    /////
    int g_usb_at_cmd;
    struct pollfd g_controlfd_at_cmd;
    unsigned char *g_recv_buffer;
    unsigned char *g_send_buffer;

    /////
    int g_total_image_count;
    int g_fw_image_count;
    int g_oem_image_count;
    char g_partition_name[MAX_DOWNLOAD_FW_IMAGES+MAX_DOWNLOAD_OEM_IMAGES][16];
    int g_image_offset[MAX_DOWNLOAD_FW_IMAGES+MAX_DOWNLOAD_OEM_IMAGES];
    int g_image_size[MAX_DOWNLOAD_FW_IMAGES+MAX_DOWNLOAD_OEM_IMAGES];
    int g_pri_offset[MAX_DOWNLOAD_PRI_IMAGES];
    int g_pri_size[MAX_DOWNLOAD_PRI_IMAGES];
    int g_pri_count;

    int download_process_state;
    int device_idx;
    int unlock_key;
    int total_device_count;

    int update_oem_pri;
    int error_code;

} fdtl_data_t;

int process_at_command( fdtl_data_t *fdtl_data, int case_id, void *parameter_1, void *parameter_2 );
void output_message_to_file( char *msg_buf );
int start_flash_image_file( char *image_file, char *gp_first_temp_file_name );
int write_temp_image_file( const char *image_file_name, int offset, int size, int pri_image, char *temp_file_name, char *prefix_string );
char compare_version_id( char *build_id_1, char *build_id_2, char last_ver_bigger );
void printf_fdtl_d( char *debug_msg );
void printf_fdtl_s( char *msg );

int fastboot_main( int exe_case, char *argv1, char *argv2, char *argv3, int device_idx );
int check_fastboot_download_port( char *argv );
int download_process( void *argu_ptr );

//#define printf_fdtl_s(stringbuffer) (output_message_to_file( stringbuffer))
//#define printf_fdtl_s(stringbuffer) (printf(stringbuffer))

