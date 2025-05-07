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

#include <pthread.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <time.h>

#include "pwl_fwupdate.h"
#include "common.h"
#include "dbus_common.h"
#include "extra_fb_struct.h"
#include "fdtl.h"


#define APPNAME "fw_update"
#define VERSION "0.0.1"

#define STOP_MODEM_MANAGER 0
#define START_MODEM_MANAGER 1

#define FASTBOOT_MODE   1

#define DOWNLOAD_INIT            1
#define DOWNLOAD_START           2
#define DOWNLOAD_FASTBOOT_START  3
#define DOWNLOAD_FASTBOOT_END    4
#define DOWNLOAD_COMPLETED       5
#define DOWNLOAD_FAILED          6
#define SUPPORT_MAX_DEVICE    10
#define SKIP_SKU_CHECK              0

#define FW_VERSION_LENGTH   12
#define OEM_PRI_VERSION_LENGTH  12
#define OTHER_VERSION_LENGTH   36
#define USB_SERIAL_BUF_SIZE  1024

static pwl_device_type_t g_device_type = PWL_DEVICE_TYPE_UNKNOWN;
static GMainLoop *gp_loop = NULL;
static pwlCore *gp_proxy = NULL;
static gulong g_ret_signal_handler[RET_SIGNAL_HANDLE_SIZE];

char g_image_file_fw_list[MAX_DOWNLOAD_FW_IMAGES][MAX_PATH];
char g_image_file_carrier_list[MAX_DOWNLOAD_PRI_IMAGES][MAX_PATH];
char g_image_file_oem_list[MAX_DOWNLOAD_OEM_IMAGES][MAX_PATH];
char g_image_file_list[MAX_DOWNLOAD_FILES][MAX_PATH];
char g_image_pref_version[MAX_PATH];
char g_diag_modem_port[SUPPORT_MAX_DEVICE][MAX_PATH];
pthread_t g_thread_id[SUPPORT_MAX_DEVICE];
char g_authenticate_key_file_name[MAX_PATH];
char g_pref_carrier[MAX_PATH];
char g_skuid[PWL_MAX_SKUID_SIZE] = {0};
char g_module_sku_id[PWL_MAX_SKUID_SIZE] = {0};
char *g_oem_sku_id;
char g_device_package_ver[DEVICE_PACKAGE_VERSION_LENGTH];
char g_current_fw_ver[FW_VERSION_LENGTH] = {0};
char g_oem_pri_ver[OEM_PRI_VERSION_LENGTH];
char g_carrier_id[10] = {0};
gboolean g_need_cxp_reboot = FALSE;
gboolean gb_del_tune_code_ret = FALSE;
gboolean gb_set_oem_pri_ver_ret = FALSE;
gboolean gb_set_pref_carrier_ret = FALSE;
gboolean g_is_get_fw_ver = FALSE;
gboolean g_retry_fw_update = FALSE;
gboolean g_has_update_include_fw_img = FALSE;
gboolean g_has_update_include_oem_img = FALSE;
gboolean g_only_flash_oem_image = FALSE;

int g_pref_carrier_id = 0;
int MAX_PREFERRED_CARRIER_NUMBER = 15;
// char *g_pref_carrier;

char g_set_pref_version = 0;
char g_download_from_fastboot_directly = 0;
bool g_fb_installed = false;
int g_fw_image_file_count = 0;
int g_image_file_count = -1;
int g_device_count = -1;
char g_display_current_time = 0;
char g_allow_image_downgrade = 0;
char g_output_debug_message = 0;
int g_oem_pri_reset_state = -1;

// For GPIO reset
gboolean g_is_fastboot_cmd_error = FALSE;
// int g_check_fastboot_retry_count;
// int g_wait_modem_port_retry_count;
// int g_wait_at_port_retry_count;
int g_do_hw_reset_count;
int g_fw_update_retry_count;
int g_need_retry_fw_update;
bool g_need_update = false;

static signal_callback_t g_signal_callback;
static bool register_client_signal_handler(pwlCore *p_proxy);

// For SKU test
char g_test_sku_id[16];

const char g_support_option[][8] = { "-f", "-d", "-impref", "-key", "-h", "-ifb", "-dfb", "-time", "-debug" };
const char g_preferred_carriers[15][MAX_PATH] = {
    "ATT",
    "DOCOMO",
    "DT",
    "EE",
    "GENERIC",
    "KDDI",
    "Optus",
    "SBM",
    "Swisscom",
    "Telefonica",
    "TELSTRA",
    "TMO",
    "VERIZON",
    "VODAFONE",
    "Rakuten"
};
//////
const char *gp_first_temp_file_name = "FirstTemp";
const char *gp_image_temp_file_name = "TempImage";
const char *gp_pri_temp_file_name = "TempImagePri";
const char *gp_decode_key_temp_file_name = "download_temp.dat";
const char *gp_log_output_file = "log.txt";

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;

pthread_mutex_t g_madpt_wait_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_madpt_wait_cond = PTHREAD_COND_INITIALIZER;

// Process percent
FILE *g_progress_fp = NULL;
int g_progress_percent = 0;
char g_progress_percent_text[32];
char g_progress_status[200];
char g_progress_command[1024];
char env_variable[64] = {0};
int env_variable_length = 64;
char set_env_variable[256] = {0};

// For pcie device
char g_pcie_download_image_list[MAX_DONWLOAD_IMAGES][MAX_IMG_FILE_NAME_LEN];
char g_t7xx_mode_node[MAX_IMG_FILE_NAME_LEN] = {0};
char g_t7xx_mode_remove_node[MAX_IMG_FILE_NAME_LEN] = {0};
char g_current_md_ver[OTHER_VERSION_LENGTH] = {0};
char g_current_op_ver[OTHER_VERSION_LENGTH] = {0};
char g_current_oem_ver[OTHER_VERSION_LENGTH] = {0};
char g_current_dpv_ver[OTHER_VERSION_LENGTH] = {0};
char g_pcie_fastboot_port[32] = {0};
int  g_pcie_img_number_count = 0;
int  g_update_type = 0;
int  g_update_based_type = 0;
int  g_is_fw_update_processing = NOT_IN_FW_UPDATE_PROCESSING;
char g_dpv_image_checksum[MAX_CHECKSUM_LEN] = {0};
gboolean g_esim_enable = TRUE;  // Default esim is enable

#define GET_KEY_FROM_IMAGE 

// Defining comparator function as per the requirement 
static int myCompare(const void* a, const void* b) 
{ 
    // setting up rules for comparison 
    return strcmp(*(const char**)a, *(const char**)b); 
} 
 
// Function to sort the array 
void sort(const char* arr[], int n) 
{ 
    // calling qsort function to sort the array 
    // with the help of Comparator 
    qsort(arr, n, sizeof(const char*), myCompare); 
} 
const char* strstricase(const char* str, const char* subStr)
{
    int len = strlen(subStr) - 1;   //ignore the last '\0'
    if(len == 0)
    {
        return NULL;
    }

    while(*str)
    {
        if(strncasecmp(str, subStr, len) == 0)
        {
            return str;
        }
        ++str;
    }
    return NULL;
}

char* get_test_sku_id()
{
    FILE *fp = NULL;
    char line[16];
    char test_sku_id[16];
    if (0 == access("/opt/pwl/test_sku_id", F_OK)) {
        fp = fopen("/opt/pwl/test_sku_id", "r");

        if (fp == NULL) {
            PWL_LOG_ERR("Open file error! return 4131001\n");
            return "4131001";
        }
        // Get test sku id from file
        while (fgets(line, 16, fp) != NULL)
        {
            if (line[0] == '\n')
                break;
            strncpy(test_sku_id, line, 7);
            test_sku_id[strlen(line)] = '\0';
        }
    } else {
        PWL_LOG_DEBUG("test sku id file not exist, create");
        fp = fopen("/opt/pwl/test_sku_id", "w");

        if (fp == NULL) {
            PWL_LOG_ERR("Create fail, return 4131001");
            return "4131001";
        }

        fprintf(fp, "4131001");
        fclose(fp);
        return "4131001";
    }
    sprintf(g_test_sku_id, "%s", test_sku_id);
    return g_test_sku_id;
}

char* get_oem_sku_id(char *ssid)
{
    if (GET_TEST_SKU_ID)
        return get_test_sku_id();

    if (strcmp(ssid, "0CBD") == 0)
        return "4131002";
    else if (strcmp(ssid, "0CC1") == 0)
        return "4131003";
    else if (strcmp(ssid, "0CC4") == 0)
        return "4131003";
    else if (strcmp(ssid, "0CB5") == 0)
        return "4131001";
    else if (strcmp(ssid, "0CB7") == 0)
        return "4131001";
    else if (strcmp(ssid, "0CB2") == 0)
        return "4131001";
    else if (strcmp(ssid, "0CB3") == 0)
        return "4131001";
    else if (strcmp(ssid, "0CB4") == 0)
        return "4131001";
    else if (strcmp(ssid, "0CB9") == 0)
        return "4131001";
    else if (strcmp(ssid, "0CBA") == 0)
        return "4131001";
    else if (strcmp(ssid, "0CBB") == 0)
        return "4131001";
    else if (strcmp(ssid, "0CBC") == 0)
        return "4131001";
    else if (strcmp(ssid, "0CD9") == 0)
        return "4131001";
    else if (strcmp(ssid, "0CDA") == 0)
        return "4131001";
    else if (strcmp(ssid, "0CF4") == 0)
        return "4131007";
    else if (strcmp(ssid, "0CE8") == 0)
        return "4131009";
    else if (strcmp(ssid, "0CF9") == 0)
        return "4131009";
    else if (strcmp(ssid, "0CF5") == 0)
        return "4131008";
    else if (strcmp(ssid, "0CF6") == 0)
        return "4131008";
    else if (strcmp(ssid, "0D5F") == 0)
        return "4131009";
    else if (strcmp(ssid, "0D60") == 0)
        return "4131009";
    else if (strcmp(ssid, "0D5C") == 0)
        return "4131009";
    else if (strcmp(ssid, "0D5E") == 0)
        return "4131009";
    else if (strcmp(ssid, "0D47") == 0)
        return "4131007";
    else if (strcmp(ssid, "0D48") == 0)
        return "4131007";
    else if (strcmp(ssid, "0D49") == 0)
        return "4131007";
    else if (strcmp(ssid, "0D61") == 0)
        return "4131007";
    else if (strcmp(ssid, "0D4D") == 0)
        return "4131008";
    else if (strcmp(ssid, "0D4E") == 0)
        return "4131008";
    else if (strcmp(ssid, "0D4F") == 0)
        return "4131008";
    else if (strcmp(ssid, "0D65") == 0)
        return "4131008";
    else
        return "4131001";
}

int get_env_variable(char env_variable[],int length)
{
    FILE *env_variable_fp = NULL;
    FILE *env_variable_x11_fp = NULL;
    char get_env_variable_cmd[] = "find /run/user -name \".mutter-Xwaylandauth*\" 2>/dev/null | head -1";
    char get_env_variable_x11_cmd[] = "find /run/user/ -name \"Xauthority\" 2>/dev/null | head -1";
    int ret = 0;

    env_variable_fp = popen(get_env_variable_cmd,"r");
    if(NULL == env_variable_fp){
        perror("get_env_variable error");
        printf("open env_variable error\n");
        return -1;
    }

    ret = fread(env_variable, sizeof(char), length, env_variable_fp);
    if(!ret){
        printf("read env_variable error\n");
        pclose(env_variable_fp);

        env_variable_x11_fp = popen(get_env_variable_x11_cmd,"r");
        if(NULL == env_variable_x11_fp){
            perror("get_env_variable_x11 error");
            printf("open get_env_variable_x11 error\n");
            return -1;
        }
        ret = fread(env_variable,sizeof(char),length,env_variable_x11_fp);
        if(!ret){
            printf("read env_variable_x11 error\n");
            pclose(env_variable_x11_fp);
            return -1;
        }
        pclose(env_variable_x11_fp);
    } else {
        pclose(env_variable_fp);
    }
    // printf("env_var: %s\n", env_variable);
    sprintf(set_env_variable, "export DISPLAY=\":0\"\nexport XDG_CURRENT_DESKTOP=\"ubuntu:GNOME\"\nexport XAUTHORITY=%s\n", env_variable);
    return 0;
}

void update_progress_dialog(int percent_add, char *message, char *additional_message)
{
    if (percent_add >= 100)
        g_progress_percent = 100;
    else
        g_progress_percent += percent_add;
    sprintf(g_progress_percent_text, "%d\n", g_progress_percent);
#if (0) // remove text status update on popup message box
    if (additional_message == NULL)
        sprintf(g_progress_status, "# %s\n", message);
    else
        sprintf(g_progress_status, "# %s %s\n", message, additional_message);
#else
    memset(g_progress_status, 0, sizeof(g_progress_status));
    if (additional_message != NULL) {
        strcpy(g_progress_status, additional_message);
    }
#endif
    if (g_progress_fp != NULL) {
        fwrite(g_progress_percent_text, sizeof(char), strlen(g_progress_percent_text), g_progress_fp);
        fwrite(g_progress_status, sizeof(char), strlen(g_progress_status), g_progress_fp);
        fflush(g_progress_fp);
    }
}

int check_fastboot_device()
{
    FILE *fp = NULL;
    char output[1024];

    memset( output, 0, 1024 );

    PWL_LOG_DEBUG("[Wait for fastboot mode]");
    fp = popen( "fastboot devices", "r" );
    if( fp == NULL )
    {
        PWL_LOG_ERR("fastboot fp is null");
        return -1;
    }

    while( fgets( output, sizeof(output), fp ) != NULL ) 
    {
        if (DEBUG) PWL_LOG_DEBUG("%s", output);
        if (strstr(output, " fastboot"))
        {
            pclose(fp);
            return 1;
        }
    }
    pclose(fp);
    return -1;
}

bool check_fastboot()
{
    char fastboot_version_cmd[] = "fastboot --version";
    FILE *fp = popen(fastboot_version_cmd, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("fastboot check cmd error!!!");
        return FALSE;
    }

    char response[200];
    memset(response, 0, sizeof(response));
    while( fgets( response, sizeof(response), fp ) != NULL ) 
    {
        if (strstr(response, "Installed"))
        {
            pclose(fp);
            return true;
        }
        sleep(5);
    }
    pclose(fp);
    return false;
}

void stop_modem_mgr()
{
    FILE *fp = NULL;
    char output[1024];
    char *pos, *result;
    int modem_manager_status = 0;

    fp = popen( "sudo systemctl status ModemManager | grep Active 2>&1", "r" );
    if( fp )
    {
        while( fgets( output, sizeof(output), fp ) != NULL ) 
        {
           pos = strstr( output, "Active"); 
           if( pos )
           {
               pos += strlen("Active");

               result = strstr( pos, "inactive"); 
               if( result )   
               {
                   modem_manager_status = -1;
                   break;
               }
               else
               { 
                   result = strstr( pos, "active"); 
                   if( result )   
                   {
                       modem_manager_status = 1;
                       break;
                   }
               }
           }
        }
        pclose( fp );
    }
    if( modem_manager_status == 1 )
    {
        fp = popen( "sudo systemctl stop ModemManager", "r" );
        if( fp )   pclose( fp ); 
        else       modem_manager_status = 0; 
    }
    if( modem_manager_status == 0 )
    {
        printf( "Not able to stop Modem Manager, please stop it by manual.\n" );
    }
}

void send_message_queue(uint32_t cid) {
    mqd_t mq;
    mq = mq_open(CID_DESTINATION(cid), O_WRONLY);

    // message to be sent
    msg_buffer_t message;
    message.pwl_cid = cid;
    message.status = PWL_CID_STATUS_NONE;
    message.sender_id = PWL_MQ_ID_FWUPDATE;

    // msgsnd to send message
    mq_send(mq, (gchar *)&message, sizeof(message), 0);
}

void send_message_queue_with_content(uint32_t cid, char *content) {
    mqd_t mq;
    mq = mq_open(CID_DESTINATION(cid), O_WRONLY);

    // message to be sent
    msg_buffer_t message;
    message.pwl_cid = cid;
    message.status = PWL_CID_STATUS_NONE;
    message.sender_id = PWL_MQ_ID_FWUPDATE;
    strcpy(message.content, content);

    // msgsnd to send message
    mq_send(mq, (gchar *)&message, sizeof(message), 0);
}

void* msg_queue_thread_func() {
    mqd_t mq;
    struct mq_attr attr;
    msg_buffer_t message;

    /* initialize the queue attributes */
    attr.mq_flags = 0;
    attr.mq_maxmsg = PWL_MQ_MAX_MSG;
    attr.mq_msgsize = sizeof(message);
    attr.mq_curmsgs = 0;

    /* create the message queue */
    mq = mq_open(PWL_MQ_PATH_FWUPDATE, O_CREAT | O_RDONLY, 0644, &attr);

    while (1) {
        ssize_t bytes_read;

        /* receive the message */
        bytes_read = mq_receive(mq, (gchar *)&message, sizeof(message), NULL);

        print_message_info(&message);

        switch (message.pwl_cid)
        {
            /* CID request from others */

            /* CID request from myself */
            case PWL_CID_GET_ATE:
                PWL_LOG_DEBUG("CID ATE: %s", message.response);
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_ATI:
                if (DEBUG && message.status == PWL_CID_STATUS_OK) PWL_LOG_DEBUG("CID ATI: %s", message.response);
                pthread_cond_signal(&g_cond);
                // pthread_exit(NULL);
                break;
            case PWL_CID_GET_OEM_PRI_INFO:
                PWL_LOG_DEBUG("OEM PRI Info: %s", message.response);
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_MAIN_FW_VER:
                PWL_LOG_DEBUG("MAIN FW VER: %s", message.response);
                // pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_AP_VER:
                if (g_device_type == PWL_DEVICE_TYPE_USB) {
                    if (strlen(message.response) > 3) {
                        // TODO: Find a good way to detect if correct AP version
                        if (strncmp(message.response, "00.", 3) == 0) {
                            strcpy(g_current_fw_ver, message.response);
                            PWL_LOG_DEBUG("AP VER: %s", g_current_fw_ver);
                            g_is_get_fw_ver = TRUE;
                        } else {
                            PWL_LOG_ERR("AP VER not match");
                            g_is_get_fw_ver = FALSE;
                        }

                    } else {
                        PWL_LOG_ERR("AP VER Error");
                        g_is_get_fw_ver = FALSE;
                    }
                } else if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
                    if (strlen(message.response) > 3) {
                        strcpy(g_current_fw_ver, message.response);
                        PWL_LOG_DEBUG("AP VER: %s", g_current_fw_ver);
                        g_is_get_fw_ver = TRUE;
                    } else {
                        PWL_LOG_ERR("AP VER not match");
                        g_is_get_fw_ver = FALSE;
                    }
                } else {
                    PWL_LOG_ERR("Device type unknow, abort!");
                    g_is_get_fw_ver = FALSE;
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_MD_VER:
                if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
                    if (strlen(message.response) > 3) {
                        if (strncmp(message.response, "RMM", 3) == 0) {
                            strcpy(g_current_md_ver, message.response);
                            PWL_LOG_DEBUG("MD VER: %s", g_current_md_ver);
                        }
                    }
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_OP_VER:
                if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
                    if (strlen(message.response) > 3) {
                        if (strncmp(message.response, "OP.", 3) == 0) {
                            strcpy(g_current_op_ver, message.response);
                            PWL_LOG_DEBUG("OP VER: %s", g_current_op_ver);
                        }
                    }
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_OEM_VER:
                if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
                    if (strlen(message.response) > 3) {
                        if (strncmp(message.response, "OEM", 3) == 0) {
                            strcpy(g_current_oem_ver, message.response);
                            PWL_LOG_DEBUG("OEM VER: %s", g_current_oem_ver);
                        }
                    }
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_DPV_VER:
                if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
                    if (strlen(message.response) > 3) {
                        if (strncmp(message.response, "DPV", 3) == 0) {
                            strcpy(g_current_dpv_ver, message.response);
                            PWL_LOG_DEBUG("DPV VER: %s", g_current_dpv_ver);
                        }
                    }
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_SWITCH_TO_FASTBOOT:
                PWL_LOG_DEBUG("switch fastboot status %s, %s", cid_status_name[message.status], message.response);
                if (strcmp(cid_status_name[message.status], "ERROR") == 0) {
                    PWL_LOG_ERR("Switch to fastboot cmd ERROR");
                    g_is_fastboot_cmd_error = TRUE;
                }
                break;
            case PWL_CID_CHECK_OEM_PRI_VERSION:
                if (message.status == PWL_CID_STATUS_OK) {
                    if (DEBUG) PWL_LOG_DEBUG("OEM VER: %s", message.response);
                    gchar* found = strstr(message.response, "Revision: ");
                    if (found != NULL) {
                        gchar* ver = found + strlen("Revision: ");
                        strncpy(g_oem_pri_ver, ver, OEM_PRI_VERSION_LENGTH - 1);
                        PWL_LOG_INFO("OEM VER: %s", g_oem_pri_ver);
                    } else {
                        PWL_LOG_ERR("Failed to get oem pri version!");
                    }
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_PREF_CARRIER:
                if (message.status == PWL_CID_STATUS_OK) {
                    if (DEBUG) PWL_LOG_DEBUG("PREF Carrier: %s", message.response);
                    char *sub_result = strstr(message.response, "preferred carrier name: ");
                    if (sub_result) {
                        int start_pos = sub_result - message.response + strlen("preferred carrier name:  ");
                        sub_result = strstr(message.response, "preferred config name");
                        int end_pos = sub_result - message.response;
                        int sub_str_size = end_pos - start_pos - 2;
                        strncpy(g_pref_carrier, &message.response[start_pos], sub_str_size);
                        for (int n=0; n < MAX_PREFERRED_CARRIER_NUMBER; n++) {
                            if (DEBUG) PWL_LOG_DEBUG("Compare: %s with %s", g_pref_carrier, g_preferred_carriers[n]);
                            if (strcasecmp(g_pref_carrier, g_preferred_carriers[n]) == 0)
                                g_pref_carrier_id = n;
                        }
                    } else {
                        PWL_LOG_ERR("Can't find preferred carrier!");
                    }
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_SKUID:
                if (message.status == PWL_CID_STATUS_OK) {
                    strcpy(g_skuid, message.response);
                } else {
                    memset(g_skuid, 0, sizeof(g_skuid));
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_DEL_TUNE_CODE:
                if (message.status == PWL_CID_STATUS_OK) {
                    gb_del_tune_code_ret = TRUE;
                    if (DEBUG) PWL_LOG_DEBUG("Del tune code result: %s", message.response);
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_SET_PREF_CARRIER:
                if (message.status == PWL_CID_STATUS_OK) {
                    gb_set_pref_carrier_ret = TRUE;
                    if (DEBUG) PWL_LOG_DEBUG("Set preferred carrier result: %s", message.response);
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_SET_OEM_PRI_VERSION:
                if (message.status == PWL_CID_STATUS_OK) {
                    gb_set_oem_pri_ver_ret = TRUE;
                    if (DEBUG) PWL_LOG_DEBUG("Set oem pri version result: %s", message.response);
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_SIM_CARRIER:
                if (message.status == PWL_CID_STATUS_OK) {
                    PWL_LOG_DEBUG("Sim Carrier: %s", message.response);
                    strcpy(g_pref_carrier, message.response);
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_MADPT_RESTART:
                pthread_cond_signal(&g_madpt_wait_cond);
                get_fw_update_status_value(NEED_RETRY_FW_UPDATE, &g_need_retry_fw_update);
                if (g_need_retry_fw_update == 1) {
                    PWL_LOG_INFO("Retry fw update");
                    set_fw_update_status_value(NEED_RETRY_FW_UPDATE, 0);
                    g_retry_fw_update = TRUE;
                }
                break;
            case PWL_CID_GET_PREF_CARRIER_ID:
                if (strlen(message.response) > 0 && (strlen(message.response) < sizeof(g_carrier_id))) {
                    PWL_LOG_DEBUG("Carrier ID: %s", message.response);
                    strcpy(g_carrier_id, message.response);
                } else {
                    PWL_LOG_ERR("Carrier id abnormal, clear g_carrier_id");
                    memset(g_carrier_id, 0, sizeof(g_carrier_id));
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_CXP_REBOOT_FLAG:
                if (strlen(message.response) > 0) {
                    if (strncmp(message.response, "1", 1) == 0)
                        g_need_cxp_reboot = TRUE;
                    else
                        g_need_cxp_reboot = FALSE;
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_OEM_PRI_RESET_STATE:
                if (strlen(message.response) > 0) {
                    get_oempri_reset_state(message.response);
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_MODULE_SKU_ID:
                if (strlen(message.response) > 0) {
                    if (strstr(message.response, "BSKU"))
                        parse_sku_id(message.response, g_module_sku_id);
                    else
                        PWL_LOG_ERR("Get sku response error!");
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_SETUP_JP_FCC_CONFIG:
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_ESIM_STATE:
                PWL_LOG_DEBUG("[ESIM] %s", message.response);
                update_esim_enable_state(message.response);
                pthread_cond_signal(&g_cond);
                break;
            default:
                PWL_LOG_ERR("Unknown pwl cid: %d", message.pwl_cid);
                break;
        }
    }

    return NULL;
}

void* monitor_retry_func() {
    while (TRUE) {
        sleep(5);
        if (g_retry_fw_update) {
            g_retry_fw_update = FALSE;
            PWL_LOG_INFO("Retry firmware update...");
            start_update_process(FALSE);
        }
    }

    return NULL;
}

int check_modem_download_port( char *modem_usb_port )
{
    FILE *fp = NULL;
    char output[1024], input[128];
    
    sprintf( input, "ls %s 2>&1", modem_usb_port );
    memset( output, 0, 1024 );

    fp = popen( input, "r" );

    if (fp) {
        if (fgets(output, 1024, fp) != NULL) {

            // if( strncmp( output, modem_usb_port, strlen(modem_usb_port) ) == 0 )  {  pclose( fp ); return 1; }
            // else        {   pclose( fp ); return 0; }
            if (strstr(output, "cannot access") == NULL) {
                strcpy(g_diag_modem_port[0], output);
                PWL_LOG_DEBUG("\nmodem port: %s", g_diag_modem_port[0]);
                pclose(fp);
                return 1;
            } else {
                pclose(fp);
                return 0;
            }
        }
    } else {
        return 0;
    }
    return 0;
}

#if 0
int check_fastboot_download_port()
{
    FILE *fp = NULL;
    char output[1024];
    char *pos;
    
    memset( output, 0, 1024 );

    fp = popen( "lsusb 2>&1", "r" );
    if( fp == NULL )    return 0;
    while( 1 )
    {
        if( fgets( output, 1024, fp ) == NULL )     break;
        //printf("lsusb: %s\n", output );

        if( strstr( output, ":c082" ) )
        {    
            pclose( fp );
            return 1;
        }
    }

    pclose( fp );
    return 0;
}
#endif

void make_prefix_string( fdtl_data_t *fdtl_data, int device_count )
{
   char usb_port[5];
   int Len;
 
   Len = strlen( fdtl_data->modem_port );
   if( (Len >= 4) && (device_count > 1) )   
   {
       strcpy( usb_port, &(fdtl_data->modem_port[Len-4]) );
       sprintf( fdtl_data->g_prefix_string, "[%s]", usb_port );
   }
   else      strcpy( fdtl_data->g_prefix_string, "" );
   
}

void printf_fdtl_s( char *msg )
{
     //PWL_LOG_DEBUG("%s", msg);
     //fflush(stdout);
}

void printf_fdtl_d( char *debug_msg )
{
     if( g_output_debug_message )    
     {
         printf_fdtl_s( debug_msg );
         fflush(stdout);
     }
}

int post_process_fastboot( fdtl_data_t *fdtl_data, FILE *fastboot_fp, int flash_step, fastboot_data_t *fastboot_data_ptr  );

int fastboot_send_command_v3( fdtl_data_t *fdtl_data, int exe_case, const char *argv1, const char *argv2, int flash_step )
{
    int rtn = 0;
    fastboot_data_t  fastboot_data;

    if( fdtl_data->total_device_count > 1 )     strcpy( fastboot_data.g_device_serial_number, fdtl_data->g_device_serial_number );
    else       strcpy( fastboot_data.g_device_serial_number, "" );

    if (g_fb_installed) {
        char command[100];
        // PWL_LOG_DEBUG("exe_case: %d 1: %s 2: %s 3: %s 4: %d", exe_case, argv1, argv2, &fastboot_data, fdtl_data->device_idx);

        switch (exe_case)
        {
            case FASTBOOT_EARSE_COMMAND:
                sprintf(command, "fastboot earse %s 2>&1", argv1);
                break;
            case FASTBOOT_REBOOT_COMMAND:
                strcpy(command, "fastboot reboot 2>&1");
                break;
            case FASTBOOT_FLASH_COMMAND:
                sprintf(command, "fastboot flash %s %s 2>&1", argv1, argv2);
                break;
            case FASTBOOT_OEM_COMMAND:
                if (argv2 != NULL)
                    sprintf(command, "fastboot oem %s %s %s 2>&1", argv1, argv2, (char *)&fastboot_data);
                else
                    sprintf(command, "fastboot oem %s %s 2>&1", argv1, (char *)&fastboot_data);
                break;
            case FASTBOOT_FLASHING_COMMAND:
                if (argv2 != NULL)
                    sprintf(command, "fastboot flashing %s %s %s 2>&1", argv1, argv2, (char *)&fastboot_data);
                else
                    sprintf(command, "fastboot flashing %s %s 2>&1", argv1, (char *)&fastboot_data);
                break;
            default:
                break;
        }
        FILE *fp = NULL;
        fp = popen(command, "r");
        if (fp == 0)
        {
            PWL_LOG_DEBUG("Send fastboot command error");
            return -1;
        }
        rtn = post_process_fastboot( fdtl_data, fp, flash_step, &fastboot_data );
    }
    else
    {
        fastboot_main( exe_case, (char *)argv1, (char *)argv2, (char *)&fastboot_data, fdtl_data->device_idx );
        rtn = post_process_fastboot( fdtl_data, 0, flash_step, &fastboot_data );
    }
    return rtn;
}

void setup_temp_file_name( fdtl_data_t *fdtl_data )
{
    sprintf( fdtl_data->g_first_temp_file_name, "%s_%s.bin", gp_first_temp_file_name, fdtl_data->g_device_serial_number );
    sprintf( fdtl_data->g_image_temp_file_name, "%s_%s.bin", gp_image_temp_file_name, fdtl_data->g_device_serial_number );
    sprintf( fdtl_data->g_pri_temp_file_name, "%s_%s.bin", gp_pri_temp_file_name, fdtl_data->g_device_serial_number );
} 

int setup_parameter( int Argc, char **Argv )
{
  if( Argc < 2 )  
  {
      printf_fdtl_s( "Invalid Parameter \n" ); 
      return -1;
  }

  int i, c;
  int set_parameter = -1;
  int get_image_file_list = -1;
  int get_device_list = -1;

  g_image_file_count = 0;
  g_device_count = 0;

  g_allow_image_downgrade = 0;
  g_output_debug_message = 0;

  for( i = 1 ; i < Argc ; i++ )
  {
     //sprintf( output_message, "Input parameter %s \n", Argv[i] );
     //printf_fdtl_s( output_message );

     for( c = 0 ; c < MAX_OPTION_NUMBER ; c++ )
     {
        if( strcmp( Argv[i], g_support_option[c] ) == 0 )
        {
           set_parameter = c;
           get_image_file_list = get_device_list = -1;

           //sprintf( output_message, "Find parameter: %d, %s \n", i, g_support_option[c] );
           //printf_fdtl_s( output_message );

           if( set_parameter <= 3 )   i++;  //Get next parameter

        break;
        }
     }

     if( (c == MAX_OPTION_NUMBER ) && ( get_image_file_list == -1 ) && ( get_device_list == -1 ) )    
     {
         printf_fdtl_s( "No image file specified. \n" );
         return -1;
     }
     if( i >= Argc )
     {
         printf_fdtl_s( "Invalid Parameter \n" );
         return -2;
     }

     if( get_image_file_list > 0 ) 
     {
         if( g_image_file_count >= MAX_DOWNLOAD_FILES )     
         {
              printf_fdtl_s("Download image files support up to 10 \n" );
              return -3;
         }
         strcpy( g_image_file_list[ g_image_file_count ] , Argv[i] ); 

         //sprintf( output_message, "Find image file: %d, %s \n", g_image_file_count, g_image_file_list[ g_image_file_count ] );
         //printf_fdtl_s( output_message );

         g_image_file_count++;
     }
     if( get_device_list > 0 ) 
     {
         if( get_device_list >= SUPPORT_MAX_DEVICE )     
         {
              printf_fdtl_s("Multiple device support up to 10 \n" );
              return -3;
         }
         strcpy( g_diag_modem_port[g_device_count] , Argv[i] ); 

         //sprintf( output_message, "Find mdeom port: %d, %s \n", g_device_count, g_diag_modem_port[ g_device_count ] );
         //printf_fdtl_s( output_message );

         g_device_count++;
     }
     else if( ( set_parameter >= 0 ) && ( set_parameter < MAX_OPTION_NUMBER ) )
     {
        switch( set_parameter )
        {
          case 0: strcpy( g_image_file_list[ g_image_file_count ], Argv[i] ); 

                  //sprintf( output_message, "Find image file: %d, %s \n", g_image_file_count, g_image_file_list[ g_image_file_count ] );
                  //printf_fdtl_s( output_message );

                  get_image_file_list = 1; 
                  g_image_file_count++;
                  break;
          case 1: strcpy( g_diag_modem_port[g_device_count], Argv[i] ); 
                  //sprintf( output_message, "Find modem port %s \n", g_diag_modem_port[0] );
                  //printf_fdtl_s( output_message );

                  get_device_list = 1;

                  g_device_count++;
                  break;

          case 2: strcpy( g_image_pref_version, Argv[i] ); 
                  g_set_pref_version = 1;
                  //printf("Pref version %s \n", g_image_pref_version );
                  break;
          case 3: if( strcmp( "1688", Argv[i] ) == 0 )              g_allow_image_downgrade = 1;
                  break;
          case 4: 
                  printf_fdtl_s("fdtl usage:\n" );
                  printf_fdtl_s("sudo ./fdtl [-f <1st image file> <2nd image file> ... ] [-d <device port>] [-impref <version>] [-dfb] [-h] \n" );
                  return 0;
          case 5: g_fb_installed = 1;
                  printf_fdtl_s("\n Using fastboot installed. \n" );
                  break;
          case 6: g_download_from_fastboot_directly = 1;
                  printf_fdtl_s("\nDownload from Fastboot mode directly. \n" );
                  break;
          case 7: g_display_current_time = 1;
                  break;
          case 8: g_output_debug_message = 1;
                  break;
        }
        set_parameter = -1;
        continue;
     }
  }
  return 1;
}

int fastboot_flash_process_v2( fdtl_data_t *fdtl_data )
{
    fdtl_data->g_pri_count = 0;

    int previous_pri_count = 0;
    int rtn, count, c;
    //char output_message[1024];

    rtn = 0;

    for( count = 0 ; count < g_image_file_count ; count++ )
    {
       fdtl_data->g_fw_image_count = 0;
       fdtl_data->g_oem_image_count = 0;
       fdtl_data->g_total_image_count = 0;

       //sprintf( output_message, "%sDownloading %s \n", fdtl_data->g_prefix_string, g_image_file_list[ count ] );
       //printf_fdtl_s( output_message );
 
        if( start_flash_image_file( g_image_file_list[ count ], fdtl_data->g_first_temp_file_name ) <= 0 )     
        {
        //    sprintf( output_message, "%sIgnore this file %s \n", fdtl_data->g_prefix_string, g_image_file_list[ count ] );
        //    printf_fdtl_d( output_message );
            PWL_LOG_DEBUG("%sIgnore this file %s \n", fdtl_data->g_prefix_string, g_image_file_list[ count ] );
            return 0;
        }
    //    sprintf( output_message, "%sflash image header, %s \n", fdtl_data->g_prefix_string, fdtl_data->g_first_temp_file_name );
    //    printf_fdtl_d( output_message );
        update_progress_dialog(1, "fastboot flash update files... ", NULL);
        PWL_LOG_DEBUG("%sflash image header, %s", fdtl_data->g_prefix_string, fdtl_data->g_first_temp_file_name );
        rtn = fastboot_send_command_v3( fdtl_data, FASTBOOT_FLASH_COMMAND, "sop-hdr", fdtl_data->g_first_temp_file_name, FASTBOOT_SOP_HDR );
        PWL_LOG_DEBUG("flash image header, result: %d", rtn);

        if( fdtl_data->g_total_image_count > 0 && rtn > 0 )
        {
            for( c = 0 ; c < fdtl_data->g_total_image_count ; c++ )        
            {
                // sprintf( output_message, "%sflash %s, %s, %d, %d \n", fdtl_data->g_prefix_string, "firmware image or oem image", fdtl_data->g_image_temp_file_name, fdtl_data->g_image_offset[c], fdtl_data->g_image_size[c] );
                // printf_fdtl_d( output_message );
                update_progress_dialog(2, "fastboot flash ", NULL);
                PWL_LOG_DEBUG("%sflash %s, %s, %d, %d \n", fdtl_data->g_prefix_string, "firmware image or oem image", fdtl_data->g_image_temp_file_name, fdtl_data->g_image_offset[c], fdtl_data->g_image_size[c] );
                write_temp_image_file( g_image_file_list[ count ], fdtl_data->g_image_offset[c], fdtl_data->g_image_size[c], 0, fdtl_data->g_image_temp_file_name, fdtl_data->g_prefix_string );

                rtn = fastboot_send_command_v3( fdtl_data, FASTBOOT_FLASH_COMMAND, fdtl_data->g_partition_name[c], fdtl_data->g_image_temp_file_name, FASTBOOT_FLASH_FW );
                PWL_LOG_DEBUG("fastboot_send_command_v3, result: %d", rtn);
                if( strstr( fdtl_data->g_partition_name[c], "mcf_c" ) != NULL )    fdtl_data->update_oem_pri = 1;
                if( rtn <= 0 )   break;  
            }
        }

       if( fdtl_data->g_pri_count > 0 && rtn > 0 )
       {
           for( c = previous_pri_count ; c < fdtl_data->g_pri_count ; c++ )       
           {
                // sprintf( output_message, "%sPrepare ca pri %s, %d, %d \n", fdtl_data->g_prefix_string, g_image_file_list[ count ], fdtl_data->g_pri_offset[c], fdtl_data->g_pri_size[c] );
                // printf_fdtl_d( output_message );
                PWL_LOG_DEBUG("%sPrepare ca pri %s, %d, %d \n", fdtl_data->g_prefix_string, g_image_file_list[ count ], fdtl_data->g_pri_offset[c], fdtl_data->g_pri_size[c] );
                write_temp_image_file( g_image_file_list[ count ], fdtl_data->g_pri_offset[c], fdtl_data->g_pri_size[c], 1, fdtl_data->g_pri_temp_file_name, fdtl_data->g_prefix_string );
           }
           previous_pri_count = fdtl_data->g_pri_count;
       }
       if( rtn <= 0 )   break;  
       //printf( " Completed. \n");
    }

    if( fdtl_data->g_pri_count > 0 && rtn > 0 )
    {
        // sprintf( output_message, "%sflash carrier image, %s \n", fdtl_data->g_prefix_string, fdtl_data->g_pri_temp_file_name );
        // printf_fdtl_d( output_message );
        PWL_LOG_DEBUG("%sflash carrier image, %s \n", fdtl_data->g_prefix_string, fdtl_data->g_pri_temp_file_name );
        rtn = fastboot_send_command_v3( fdtl_data, FASTBOOT_FLASH_COMMAND, "capri_c", fdtl_data->g_pri_temp_file_name, FASTBOOT_FLASH_PRI );
        PWL_LOG_DEBUG("flash carrier image, result: %d", rtn);
    }
    remove( fdtl_data->g_pri_temp_file_name );
    remove( fdtl_data->g_image_temp_file_name );
    remove( fdtl_data->g_first_temp_file_name );
       
    return rtn;
}

gint get_image_version(char *image_file_name, char *ver_info)
{
    FILE *fp = fopen(image_file_name, "rb");
    unsigned char id[11];
    unsigned char hsize[4];
    unsigned char v_he[512];
    unsigned int size, len;

    memset(id, 0, sizeof(id));
    memset(hsize, 0, sizeof(hsize));

    fseek(fp, 2, SEEK_SET);
    len = fread(id, 1, 10, fp);

    fseek(fp, 22, SEEK_SET);
    len = fread(hsize, 1, 4, fp);
    size = (hsize[3]<<24)+(hsize[2]<<16)+(hsize[1]<<8)+hsize[0];

    if( 0 == size ) 
    {
        //printf("No firmware info.\n");
        return -1;
    }

    memset(v_he, 0, sizeof(v_he));
    fseek(fp, 26, SEEK_SET);
    len = fread(v_he, 1, size, fp);
    if( len < size ) 
    {
         //printf("TLV header check fail.\n");
         return -1;
    }

    int record_start = 0;

    while( record_start < size )
    {
        int length = 0;

        unsigned char tmp_length[2];
        memset(tmp_length, 0, sizeof(tmp_length));
        memcpy(tmp_length, v_he+record_start+1, 2);

        length = (tmp_length[1] << 8) + tmp_length[0];

        char tmp[length];
        memset(tmp, 0, sizeof(tmp));
        memcpy(tmp, v_he+record_start+3, length);

        strncpy(ver_info, tmp, 11);
        // printf("Version: %s\n", ver_info);
        break;
    }
    fclose(fp);
    return 1;
}

int decode_key( char *image_file_name )
{
  unsigned char read_buf[ 256 ];
  int data_temp[ 256 ], i, size;
  FILE *image_fp = NULL;

  size = 0;

//   printf("Get key from image, %s \n", image_file_name );
  image_fp = fopen( image_file_name, "rb");
  if( image_fp ) 
  {
    fseek( image_fp, 0, SEEK_END );
    size = ftell( image_fp );
    if( size >= 256)  fseek( image_fp, size - 256, SEEK_SET );
    size = fread( read_buf, 1, 256, image_fp );
    // printf("Get key size %d \n", size );
  }
  else   return -1;

  if( size == 256 )
  {
          for( i = 0 ; i < 256 ; i++ )       data_temp[ 255 - i ] = (int)read_buf[ i ];
          for( i = 0 ; i < 256 ; i++ )       
          {
               data_temp[ i ] -= 3;
               if( data_temp[ i ] < 0 )    data_temp[ i ] += 256;
          }
  } 
  if( image_fp )      fclose( image_fp );

  strcpy( g_authenticate_key_file_name, gp_decode_key_temp_file_name );
  image_fp = fopen( gp_decode_key_temp_file_name, "wb");
  if( image_fp == NULL )
  {
      if( image_fp == NULL )  { printf( "Open file %s error",  gp_decode_key_temp_file_name ); return -2; }
  }

  for( i = 0 ; i < 256 ; i++ )       read_buf[ i ] = (unsigned char)data_temp[ i ];

  size = fwrite( read_buf, 1, 256, image_fp );
  fclose( image_fp );

  if( size != 256 )  return -3;

  //printf( "Decode Key Done.\n" );

  return 1;
}

void initialize_global_variable()
{
   g_fb_installed = 0;
   g_set_pref_version = 0;
   g_image_file_count = -1;
   g_download_from_fastboot_directly = 0;
   g_display_current_time = 0;
   g_allow_image_downgrade = 0;
   g_output_debug_message = 0;
}

int get_time_info( char print_time )
{
   time_t current_time = time(0);
   char *time_str = ctime(&current_time);
   time_str[strlen(time_str)-1]='\0';
   if( print_time )   printf("Current time: %s\n\n", time_str );

   return (int)current_time;
}


void close_progress_msg_box(int close_type) {
    char close_message[64] = {0};
    switch (close_type) {
        case CLOSE_TYPE_ERROR:
            sprintf(close_message, "%s", "#Modem firmware update failed!\\n\\n\n");
            break;
        case CLOSE_TYPE_SKIP:
            sprintf(close_message, "%s", "#Modem firmware is up to date\\n\\n\n");
            break;
        case CLOSE_TYPE_SUCCESS:
            sprintf(close_message, "%s", "#The Modem update Success!\\n\\n\n");
            break;
        case CLOSE_TYPE_RETRY:
            sprintf(close_message, "%s", "#Modem download error, will automatically retry later\\n\\n\n");
            break;
        default:
            break;
    }
    if (g_progress_fp != NULL) {
        g_progress_percent = 98;
        if (DEBUG) PWL_LOG_DEBUG("close message: %s", close_message);
        update_progress_dialog(1, NULL, close_message);
        g_usleep(1000*1000*3);
        update_progress_dialog(1, NULL, close_message);
        pclose(g_progress_fp);
        g_progress_fp = NULL;
    }
}

/////
void prompt_delay( int delay_count, int interval )
{
    int delay_countdown;

    for( delay_countdown = delay_count ; delay_countdown > 0 ; delay_countdown-- )
    {
         if( (delay_countdown % interval) == 0 )
         {
             printf_fdtl_s(".");
             fflush(stdout);
         }
         sleep(1);
    }
}


/////
int waiting_modem_download_port( fdtl_data_t *fdtl_data )
{
    int update_count_down;
    char output_message[1024];

    for( update_count_down = 300 ; update_count_down > 0 ; update_count_down-- )
    {
         if( update_count_down > 30 )
         {
             if( check_modem_download_port( fdtl_data->modem_port ) )  break;
         }

         if( ( (update_count_down % 5) == 0 ) && ( fdtl_data->total_device_count == 1 ) )
         {
             printf_fdtl_s(".");
             fflush(stdout);
         }
         sleep(1);
    }

    if( update_count_down == 0 )
    {
        sprintf( output_message, "\n%sCan't find USB port %s. \n", fdtl_data->g_prefix_string, fdtl_data->modem_port );
        printf_fdtl_s( output_message );
        fflush(stdout);
    }
    /*else     
    {
        sprintf( output_message, "\n%sFind USB port %s. \n", fdtl_data->g_prefix_string, fdtl_data->modem_port );
        printf_fdtl_s( output_message );
        fflush(stdout);
    }*/
    return update_count_down;
}

int download_process( void *argu_ptr )
{
    int count;
    int rtn;
    char output_message[1024];
    unsigned char *recv_buffer;
    unsigned char *send_buffer;
    int raw_time, elapsed_time;
    int check_fastboot_count = 0;

    fdtl_data_t *fdtl_data;
    fdtl_data = argu_ptr;

    fdtl_data->download_process_state = DOWNLOAD_START;

    recv_buffer = (unsigned char *) malloc(USB_SERIAL_BUF_SIZE);
    if( recv_buffer == NULL )    
    {
        fdtl_data->download_process_state = DOWNLOAD_FAILED;
        fdtl_data->error_code = MEMORY_ALLOCATION_FAILED;
        return MEMORY_ALLOCATION_FAILED;
    }
    send_buffer = (unsigned char *) malloc(USB_SERIAL_BUF_SIZE);
    if( send_buffer == NULL )    
    {
        free(recv_buffer);
        fdtl_data->download_process_state = DOWNLOAD_FAILED;
        fdtl_data->error_code = MEMORY_ALLOCATION_FAILED;
        return MEMORY_ALLOCATION_FAILED;
    }
    /////////
    memset(recv_buffer, 0, USB_SERIAL_BUF_SIZE);
    memset(send_buffer, 0, USB_SERIAL_BUF_SIZE);
    fdtl_data->g_recv_buffer = recv_buffer;
    fdtl_data->g_send_buffer = send_buffer;
    strcpy( fdtl_data->g_device_serial_number, "" );
    strcpy( fdtl_data->g_device_build_id, "" );

    fdtl_data->g_open_at_usb = false;

    raw_time = get_time_info( g_display_current_time );

    // Switch to fastboot in mbin
    update_progress_dialog(3, "Switch to fastboot mode.", NULL);
    g_is_fastboot_cmd_error = FALSE;
    send_message_queue(PWL_CID_SWITCH_TO_FASTBOOT);

    sleep(5);

    if (g_is_fastboot_cmd_error) {
        g_is_fastboot_cmd_error = FALSE;
        return SWITCHING_TO_DOWNLOAD_MODE_FAILED;
    }

    setup_temp_file_name( fdtl_data );

    sprintf(output_message, "\n%sSetting download port", fdtl_data->g_prefix_string );

    printf_fdtl_s( output_message );

    update_progress_dialog(3, "Waiting fastboot port...", NULL);
    for( count = 0 ; count < 60 ; count++ )
    {
        if (g_fb_installed)
        {
            if (check_fastboot_device() == 1) {
                PWL_LOG_DEBUG("In fastboot mode.");
                break;
            }
            else
            {
                printf_fdtl_s(".");
                fflush(stdout);
                if (check_fastboot_count < 60)
                {
                    check_fastboot_count++;
                    sleep(1);
                }
                else
                {
                    PWL_LOG_ERR("Switch to fastboot mode error, abort!");
                    // TODO: Request gpio reset here
                    return SWITCHING_TO_DOWNLOAD_MODE_FAILED;
                }
                
            }
        }
        else
        {
            if( check_fastboot_download_port( (char *)fdtl_data ) )   break;
            if( fdtl_data->total_device_count == 1 )         printf_fdtl_s(".");
            fflush(stdout);
            sleep(1);
        }
    }

    if( fdtl_data->total_device_count == 1 )       printf_fdtl_s("\n"); 

    if( count >= 60 )     
    {
        update_progress_dialog(5, "Switch to download mode failed.", NULL);
        sprintf(output_message, "\n%sSwitch to download mode failed.\n", fdtl_data->g_prefix_string );
        printf_fdtl_s( output_message );

        free(recv_buffer);
        free(send_buffer);

        update_progress_dialog(5, "Exit download process.", "#failed to enter download mode, exit from update\\n\\n\n");
        sprintf(output_message, "%sExit download process.\n\n", fdtl_data->g_prefix_string );
        printf_fdtl_s( output_message );

        fdtl_data->download_process_state = DOWNLOAD_FAILED;
        fdtl_data->error_code = SWITCHING_TO_DOWNLOAD_MODE_FAILED;
        g_usleep(1000*1000*3);
        if (g_progress_fp != NULL) {
            pclose(g_progress_fp);
            g_progress_fp = NULL;
        }
        return SWITCHING_TO_DOWNLOAD_MODE_FAILED;
    }
    //elapsed_time = get_time_info(0) - raw_time;
    //sprintf( output_message, "Switched to downlaod mode, Elapsed time: %02dm:%02ds \n", elapsed_time/60, elapsed_time%60 );
    //printf_fdtl_s( output_message );

    ////////  Pre-Setup
    fdtl_data->download_process_state = DOWNLOAD_FASTBOOT_START;

    if( fdtl_data->total_device_count > 1 )    printf("\n" );

    sprintf( output_message, "%sDownloading...\n", fdtl_data->g_prefix_string );
    printf_fdtl_s( output_message );
    fflush(stdout);

    // sprintf( output_message, "\n%sfastboot flash frp-unlock %s \n", fdtl_data->g_prefix_string, g_authenticate_key_file_name );
    // printf_fdtl_d( output_message );
    update_progress_dialog(5, "fastboot flash frp-unlock", NULL);
    PWL_LOG_DEBUG("%sfastboot flash frp-unlock %s \n", fdtl_data->g_prefix_string, g_authenticate_key_file_name );
    rtn = fastboot_send_command_v3( fdtl_data, FASTBOOT_FLASH_COMMAND, "frp-unlock", g_authenticate_key_file_name, FASTBOOT_FRP_UNLOCK_KEY );
    PWL_LOG_DEBUG("frp-unlock result: %d", rtn);
    if( rtn > 0 )       
    {
        // sprintf( output_message, "%sfastboot flashing unlock \n", fdtl_data->g_prefix_string );
        // printf_fdtl_d( output_message );
        update_progress_dialog(5, "fastboot flashing unlock", NULL);
        PWL_LOG_DEBUG("%sfastboot flashing unlock \n", fdtl_data->g_prefix_string );
        rtn = fastboot_send_command_v3( fdtl_data, FASTBOOT_FLASHING_COMMAND, "unlock", NULL, FASTBOOT_FRP_UNLOCK_KEY );
    }
    PWL_LOG_DEBUG("fastboot flashing unlock result: %d", rtn);

    if( rtn > 0 )
    {
        fdtl_data->unlock_key = 1;
        if( g_set_pref_version )
        {
            // sprintf( output_message, "%sfastboot oem impref %s \n", g_image_pref_version, fdtl_data->g_prefix_string );
            // printf_fdtl_d( output_message );
            PWL_LOG_DEBUG("%sfastboot oem impref %s \n", g_image_pref_version, fdtl_data->g_prefix_string );

            if( (rtn = fastboot_send_command_v3( fdtl_data, FASTBOOT_OEM_COMMAND, "impref", g_image_pref_version, FASTBOOT_FLASH_PREF )) <= 0 )
            {
                // sprintf( output_message, "\n%sSet prefer version failed: %s \n", fdtl_data->g_prefix_string,  g_image_pref_version );
                // printf_fdtl_s( output_message );
                PWL_LOG_DEBUG("\n%sSet prefer version failed: %s \n", fdtl_data->g_prefix_string,  g_image_pref_version );
            }
        }
    }
    if( rtn <= 0 )
    {  
        update_progress_dialog(5, "fastboot reboot...", NULL);
        PWL_LOG_DEBUG("\nrtn: %d, FASTBOOT_REBOOT_COMMAND\n", rtn);
        fastboot_send_command_v3( fdtl_data, FASTBOOT_REBOOT_COMMAND, NULL, NULL, FASTBOOT_IGNORE );

        free(recv_buffer);
        free(send_buffer);
        fdtl_data->download_process_state = DOWNLOAD_FAILED;
        fdtl_data->error_code = FASTBOOT_COMMAND_FAILED;
        return FASTBOOT_COMMAND_FAILED;
    }

    rtn = 1;

    rtn = fastboot_flash_process_v2( fdtl_data );
    PWL_LOG_DEBUG("fastboot_flash_process, result: %d", rtn);
    if( rtn <= 0 )
    {  
        PWL_LOG_DEBUG("\nrtn: %d, FASTBOOT_REBOOT_COMMAND", rtn);
        fastboot_send_command_v3( fdtl_data, FASTBOOT_REBOOT_COMMAND, NULL, NULL, FASTBOOT_IGNORE );

        free(recv_buffer);
        free(send_buffer);
        fdtl_data->download_process_state = DOWNLOAD_FAILED;
        fdtl_data->error_code = FASTBOOT_FLASHING_FAILED;
        return FASTBOOT_FLASHING_FAILED;
    }

    if( rtn > 0 )      
    { 
        // sprintf( output_message, "%sfastboot oem boot-flag 9 \n", fdtl_data->g_prefix_string );
        // printf_fdtl_d( output_message );
        update_progress_dialog(2, "fastboot oem boot-flag 9", NULL);
        PWL_LOG_DEBUG("%sfastboot oem boot-flag 9", fdtl_data->g_prefix_string );
        fastboot_send_command_v3( fdtl_data, FASTBOOT_OEM_COMMAND, "boot-flag", "9", FASTBOOT_IGNORE );
    }

    ///////////// Reboot
    // sprintf( output_message, "%sfastboot reboot \n", fdtl_data->g_prefix_string );
    // printf_fdtl_d( output_message );
    update_progress_dialog(2, "fastboot reboot...", NULL);
    PWL_LOG_DEBUG("%sfastboot reboot \n", fdtl_data->g_prefix_string );
    fastboot_send_command_v3( fdtl_data, FASTBOOT_REBOOT_COMMAND, NULL, NULL, FASTBOOT_IGNORE );

    fdtl_data->download_process_state = DOWNLOAD_FASTBOOT_END;
    if( fdtl_data->total_device_count == 1 )       printf_fdtl_s("\n\n"); 

    //elapsed_time = get_time_info(0) - raw_time;
    //sprintf( output_message, "Fasboot download finished, Elapsed time: %02dm:%02ds \n\n", elapsed_time/60, elapsed_time%60 );
    //printf_fdtl_s( output_message );

    // Because the direct downlaod from fastboot mode does not need to specify modem port, so the FW version cannot be read after the downlaod ends.
    if( g_download_from_fastboot_directly && ( strlen(fdtl_data->modem_port) == 0 ) )
    {
        // sprintf( output_message, "\n%sExit download process. \n", fdtl_data->g_prefix_string );
        // printf_fdtl_s( output_message );
        PWL_LOG_DEBUG("\n%sExit download process. \n", fdtl_data->g_prefix_string );
        fflush(stdout);

        fdtl_data->download_process_state = DOWNLOAD_COMPLETED;

        return 1;
    }

    if( fdtl_data->total_device_count > 1 )       sprintf(output_message, "\n%sUpdating the firmware.\n", fdtl_data->g_prefix_string );
    else       sprintf(output_message, "%sUpdating the firmware.\n", fdtl_data->g_prefix_string );
    printf_fdtl_s( output_message );

    sprintf(output_message, "%sThe update process takes up to 3 minutes, please wait.\n", fdtl_data->g_prefix_string );
    printf_fdtl_s( output_message );
    //printf("\n Modem port detecting \n");


    ///////////// End flash
    sprintf(output_message, "%sProcessing...", fdtl_data->g_prefix_string );
    printf_fdtl_s( output_message );
    fflush(stdout);
    update_progress_dialog(2, "Waiting for device boot...", NULL);
    if( waiting_modem_download_port( fdtl_data ) == 0 )
    {  
         free(recv_buffer);
         free(send_buffer);
         fdtl_data->download_process_state = DOWNLOAD_FAILED;
         fdtl_data->error_code = MODEM_PORT_OPENING_FAILS_AFTER_FIRMWARE_DOWNLOAD;
         return MODEM_PORT_OPENING_FAILS_AFTER_FIRMWARE_DOWNLOAD;
    }
    /*
    sprintf( output_message, "\n%sFind USB port %s", fdtl_data->g_prefix_string, fdtl_data->modem_port );
    if( fdtl_data->total_device_count > 1 )    printf_fdtl_s( output_message );
    */
    update_progress_dialog(5, "Open Mbim port...", NULL);
    PWL_LOG_DEBUG("\n%sFind USB port %s", fdtl_data->g_prefix_string, g_diag_modem_port[0]);
    elapsed_time = get_time_info(0) - raw_time;
    sprintf( output_message, "\n%sModule boot up completed, Elapsed time: %02dm:%02ds \n", fdtl_data->g_prefix_string, elapsed_time/60, elapsed_time%60 );
    printf_fdtl_s( output_message );

    // Restart MADPT
    if (g_has_update_include_oem_img) {
        send_message_queue_with_content(PWL_CID_MADPT_RESTART, "TRUE");
    } else {
        send_message_queue_with_content(PWL_CID_MADPT_RESTART, "FALSE");
    }

    if (!cond_wait(&g_madpt_wait_mutex, &g_madpt_wait_cond, 120)) {
        PWL_LOG_ERR("timed out or error for madpt restart");
    } else {
        PWL_LOG_INFO("Modem back online");
    }

    // Set preferred carrier
    // TODO record preferred carrier and download status to ROM file.
    update_progress_dialog(5, "Set preferred carrier...", NULL);
    if (set_preferred_carrier() != 0)
        PWL_LOG_ERR("Set preferred carrier fail.");

    // Set oem pri version
    if (g_has_update_include_oem_img) {
        sleep(5);
        update_progress_dialog(5, "Set oem pri version...", NULL);
        if (set_oem_pri_version() != 0)
            PWL_LOG_ERR("Set oem pri version fail.");
    }

    // Notice pwl-pref to update fw version
    sleep(1);
    send_message_queue(PWL_CID_UPDATE_FW_VER);

    // Send ATI cmd
    sleep(5);
    if (get_ati_info() != 0)
        PWL_LOG_ERR("Get ATI fail.");

    close( fdtl_data->g_usb_at_cmd );
    free(recv_buffer);
    free(send_buffer);

    if( fdtl_data->total_device_count > 1 )       sprintf( output_message, "%sExit download process. \n", fdtl_data->g_prefix_string );
    else        sprintf( output_message, "\n%sExit download process. \n", fdtl_data->g_prefix_string );
    printf_fdtl_s( output_message );
    fflush(stdout);

    g_progress_percent = 98;
    update_progress_dialog(1, "Exit download process.", "#The Modem update Success!\\n\\n\n");
    g_usleep(1000*1000*3);
    update_progress_dialog(1, "Exit download process.", "#The Modem update Success!\\n\\n\n");
    if (g_progress_fp != NULL) {
        pclose(g_progress_fp);
        g_progress_fp = NULL;
    }
    fdtl_data->download_process_state = DOWNLOAD_COMPLETED;
    fdtl_data->error_code = 1;
    return 1;
}

int extract_update_files()
{
    FILE *zip_file = NULL;
    char command[128] = {0};
    int ret = -1;
    int retry = 0;

    zip_file = fopen(UPDATE_FW_ZIP_FILE, "r");
    if (zip_file != NULL)
    {
        while (retry < PWL_FW_UPDATE_RETRY_LIMIT)
        {
            if (DEBUG)
                sprintf(command, "unzip -o %s -d %s", UPDATE_FW_ZIP_FILE, UPDATE_UNZIP_PATH);
            else
                sprintf(command, "unzip -o -qq %s -d %s", UPDATE_FW_ZIP_FILE, UPDATE_UNZIP_PATH);

            ret = system(command);

            if (!ret)
            {
                PWL_LOG_DEBUG("Extract update zip success");
                fclose(zip_file);
                return ret;
            }
            else
            {
                PWL_LOG_DEBUG("ret: %d, Extract update zip FAIL, retry!", ret);
                retry++;
                continue;
            }
        }
        fclose(zip_file);
    }
    return ret;
}

#define EVENT_NUM  12
void *monitor_package_func2()
{
    char *event_str[EVENT_NUM] =
    {
        "IN_ACCESS",
        "IN_MODIFY",
        "IN_ATTRIB",
        "IN_CLOSE_WRITE",
        "IN_CLOSE_NOWRITE",
        "IN_OPEN",
        "IN_MOVED_FROM",
        "IN_MOVED_TO",
        "IN_CREATE",
        "IN_DELETE",
        "IN_DELETE_SELF",
        "IN_MOVE_SELF"
    };

    int fd = -1;
    fd = inotify_init();
    if (fd < 0)
    {
        PWL_LOG_ERR("inotify_init failed");
        return NULL;
    }
    int wd = -1;
    struct inotify_event *event;
    int length, nread;
    char buf[BUFSIZ];
    int i = 0;
    buf[sizeof(buf) - 1] = 0;
    int already_retry_count;

INOTIFY_AGAIN:
    wd = inotify_add_watch(fd, IMAGE_MONITOR_PATH, IN_ALL_EVENTS);
    if (wd < 0)
    {
        PWL_LOG_ERR("inotify_add_watch failed");
        return NULL;
    }

    length = read(fd, buf, sizeof(buf) - 1);
    nread = 0;
    
    while (length > 0)
    {
        event = (struct inotify_event *)&buf[nread];
        for (i = 0; i < EVENT_NUM; i++)
        {
            if ((event->mask >> i) & 1)
            {
                // monitor target is folder
                if (event->len > 0)
                {
                    if(DEBUG) PWL_LOG_DEBUG("%s --- %s", event->name, event_str[i]);
                    if (strcmp(event->name, MONITOR_FOLDER_NAME) == 0 &&
                        strcmp(event_str[i], "IN_MOVED_TO") == 0) {
                        if (g_device_type == PWL_DEVICE_TYPE_USB) {
                            PWL_LOG_DEBUG("Check update zip file");
                            sleep(2);
                            if (extract_update_files() == 0) {
                                PWL_LOG_DEBUG("Start update process");
                                start_update_process(FALSE);
                            }
                        } else if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
                            if (g_is_fw_update_processing == NOT_IN_FW_UPDATE_PROCESSING) {
                                PWL_LOG_DEBUG("Check update flz file");
                                sleep(2);
                                if (check_update_data(TYPE_FLASH_FLZ) == RET_OK) {
                                    // Notice pref update version.
                                    if (DEBUG) PWL_LOG_DEBUG("[Notice] Send signal to pref to get version and wait 7 secs");
                                    pwl_core_call_request_update_fw_version_method(gp_proxy, NULL, NULL, NULL);
                                    sleep(7);
                                    if (g_is_fw_update_processing == NOT_IN_FW_UPDATE_PROCESSING) {
                                        if (start_update_process_pcie(FALSE, PCIE_UPDATE_BASE_FLZ) == RET_OK) {
                                            // Download success, remove flash data folder
                                            remove_flash_data(g_update_type);
                                            close_progress_msg_box(CLOSE_TYPE_SUCCESS);
                                            pwl_core_call_request_update_fw_version_method(gp_proxy, NULL, NULL, NULL);
                                        } else {
                                            if (g_need_update) {
                                                close_progress_msg_box(CLOSE_TYPE_RETRY);
                                                get_fw_update_status_value(FW_UPDATE_RETRY_COUNT, &already_retry_count);
                                                PWL_LOG_DEBUG("[Notice] already_retry_count: %d", already_retry_count);
                                                while (already_retry_count < FW_UPDATE_RETRY_TH) {
                                                    PWL_LOG_DEBUG("[Notice] Retry fw udpate");
                                                    if (start_update_process_pcie(TRUE, PCIE_UPDATE_BASE_FLZ) == RET_OK) {
                                                        remove_flash_data(g_update_type);
                                                        close_progress_msg_box(CLOSE_TYPE_SUCCESS);
                                                        pwl_core_call_request_update_fw_version_method(gp_proxy, NULL, NULL, NULL);
                                                        break;
                                                    } else {
                                                        get_fw_update_status_value(FW_UPDATE_RETRY_COUNT, &already_retry_count);
                                                        if (already_retry_count >= FW_UPDATE_RETRY_TH)
                                                            close_progress_msg_box(CLOSE_TYPE_ERROR);
                                                    }
                                                }
                                            }
                                        }
                                        // Check if need cxp reboot
                                        if (g_need_cxp_reboot) {
                                            g_need_cxp_reboot = FALSE;
                                            if (DEBUG) PWL_LOG_DEBUG("[CXP] Do reboot");
                                            switch_t7xx_mode(MODE_HW_RESET);
                                        }
                                    } else {
                                        PWL_LOG_DEBUG("Another update processing on going, ignore.");
                                    }
                                }
                            } else {
                                PWL_LOG_DEBUG("During fw update process, skip.");
                            }
                        }
                    }
                }
                // monitor target is file
                else if (event->len == 0)
                {
                    if (event->wd == wd)
                        if(DEBUG) PWL_LOG_DEBUG("%s >>> %s", IMAGE_MONITOR_PATH, event_str[i]);
                }
            }
        }
        nread = nread + sizeof(struct inotify_event) + event->len;
        length = length - sizeof(struct inotify_event) - event->len;
    }
    goto INOTIFY_AGAIN;
    close(fd);
    return 0;
}

gint get_sim_carrier()
{
    int err, retry = 0;
    memset(g_pref_carrier, 0, sizeof(g_pref_carrier));

    while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
        if (GET_TEST_SIM_CARRIER) {
            FILE *fp = NULL;
            char line[16];
            if (0 == access("/opt/pwl/test_carrier", F_OK)) {
                fp = fopen("/opt/pwl/test_carrier", "r");

                if (fp == NULL) {
                    PWL_LOG_ERR("Open file error!\n");
                    return -1;
                }
                // Get test carrier from file
                while (fgets(line, 16, fp) != NULL)
                {
                    if (line[0] == '\n')
                        break;
                    strcpy(g_pref_carrier, line);
                    g_pref_carrier[strlen(line)] = '\0';
                    if (DEBUG) PWL_LOG_DEBUG("Get test carrier: %s", g_pref_carrier);
                    return 0;
                }
            } else {
                PWL_LOG_ERR("Test carrier file not exist, please create and try again.");
                return -1;
            }
            return -1;
        } else {
            err = 0;
            send_message_queue(PWL_CID_GET_SIM_CARRIER);
            pthread_mutex_lock(&g_mutex);
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;

            int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
            if (result == ETIMEDOUT || result != 0) {
                PWL_LOG_ERR("Time out to get sim carrier, retry");
                err = 1;
            }
            pthread_mutex_unlock(&g_mutex);

            if (err || strlen(g_pref_carrier) == 0) {
                retry++;
                continue;
            }
            return 0;
        }
    }

    return -1;
}

gint get_preferred_carrier()
{
    int err, retry = 0;
    memset(g_pref_carrier, 0, sizeof(g_pref_carrier));

    while (retry < PWL_FW_UPDATE_RETRY_LIMIT)
    {
        err = 0;
        send_message_queue(PWL_CID_GET_PREF_CARRIER);
        pthread_mutex_lock(&g_mutex);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;

        int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
        if (result == ETIMEDOUT || result != 0) {
            PWL_LOG_ERR("Time out to get pref carrier, retry");
            err = 1;
        }
        pthread_mutex_unlock(&g_mutex);

        if (err || strlen(g_pref_carrier) == 0) {
            retry++;
            continue;
        }
        return 0;
    }
    return -1;
}

int check_oempri_reset_state() {
    int err, retry = 0;
    while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
        err = 0;
        send_message_queue(PWL_CID_GET_OEM_PRI_RESET_STATE);
        pthread_mutex_lock(&g_mutex);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;
        int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
        if (result == ETIMEDOUT || result != 0) {
            PWL_LOG_ERR("Time out to get oem pri reset state, retry");
            err = 1;
        }
        pthread_mutex_unlock(&g_mutex);
        if (err || g_oem_pri_reset_state == -1) {
            retry++;
            continue;
        }
        // Only oem pri reset fail cases (2, 3, 4, 5, 8) return RET_FAILED
        // others return RET_OK (even at cmd error)
        switch (g_oem_pri_reset_state) {
            case OEMPRI_OPEN_RFS_FAIL:
            case OEMPRI_READ_RFS_FAIL:
            case OEMPRI_SEEK_RFS_FAIL:
            case OEMPRI_CHECK_CRC_FAIL:
            case OEMPRI_READ_RFS_ABNORMAL:
                return RET_FAILED;
                break;
            case OEMPRI_SAME_VERSION:
            case OEMPRI_UPDATE_SUCCESS:
            case OEMPRI_WRITE_EFS_FAIL:
            case OEMPRI_WRITE_EFS_WRONG:
            case OEMPRI_READ_QCN_FAIL:
            case OEMPRI_READ_MAX:
                return RET_OK;
                break;
            default:
                return RET_OK;
                break;
        }
    }
    return RET_OK;
}

gint get_sku_id()
{
    int err, retry = 0;
    while (retry < PWL_FW_UPDATE_RETRY_LIMIT)
    {
        err = 0;
        send_message_queue(PWL_CID_GET_SKUID);
        pthread_mutex_lock(&g_mutex);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;
        int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
        if (result == ETIMEDOUT || result != 0) {
            PWL_LOG_ERR("Time out to get SKU ID, retry");
            err = 1;
        }
        pthread_mutex_unlock(&g_mutex);
        if (err || strlen(g_skuid) == 0) {
            retry++;
            continue;
        }
        return 0;
    }
    return -1;
}

gint get_carrier_id() {
    int err, retry = 0;
    memset(g_carrier_id, 0, sizeof(g_carrier_id));
    while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
        err = 0;
        send_message_queue(PWL_CID_GET_PREF_CARRIER_ID);
        pthread_mutex_lock(&g_mutex);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += PWL_CMD_TIMEOUT_SEC + 2; // Carrier ID took longer to read, add fewer sec
        int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
        if (result == ETIMEDOUT || result != 0) {
            PWL_LOG_ERR("Time out to get Carrier ID, retry.");
            err = 1;
        }
        pthread_mutex_unlock(&g_mutex);
        if (err) {
            retry++;
            continue;
        }
        PWL_LOG_DEBUG("[Notice] carrier id: %s", g_carrier_id);
        return RET_OK;
    }
}

gint get_cxp_reboot_flag() {
    int err, retry = 0;
    g_need_cxp_reboot = FALSE;
    while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
        err = 0;
        send_message_queue(PWL_CID_GET_CXP_REBOOT_FLAG);
        pthread_mutex_lock(&g_mutex);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += PWL_CMD_TIMEOUT_SEC + 2; // Carrier ID took longer to read, add fewer sec
        int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
        if (result == ETIMEDOUT || result != 0) {
            PWL_LOG_ERR("Time out to get CXP reboot flag, retry.");
            err = 1;
        }
        pthread_mutex_unlock(&g_mutex);
        if (err) {
            retry++;
            continue;
        }
        PWL_LOG_DEBUG("[CXP] CXP reboot: %d", g_need_cxp_reboot);
        return RET_OK;
    }
}

gint get_current_fw_version()
{
    int err, retry = 0;
    memset(g_current_fw_ver, 0, FW_VERSION_LENGTH);

    memset(g_current_fw_ver, 0, sizeof(g_current_fw_ver));
    while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
        err = 0;
        send_message_queue(PWL_CID_GET_AP_VER);
        pthread_mutex_lock(&g_mutex);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;
        int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
        if (result == ETIMEDOUT || result != 0) {
            PWL_LOG_ERR("Time out to get AP version, retry.");
            err = 1;
        }
        pthread_mutex_unlock(&g_mutex);
        if (err) {
            retry++;
            continue;
        }

        if (g_device_type == PWL_DEVICE_TYPE_USB) {
            return RET_OK;
        } else if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
            break;
        } else {
            return RET_FAILED;
        }
    }
    if(strlen(g_current_fw_ver) <= 0) {
        PWL_LOG_ERR("Get AP version error, abort!");
        return RET_FAILED;
    }

    // Get MD, OP, OEM and DPV version for pcie device
    if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
        // MD version
        memset(g_current_md_ver, 0, sizeof(g_current_md_ver));
        while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
            err = 0;
            send_message_queue(PWL_CID_GET_MD_VER);
            pthread_mutex_lock(&g_mutex);
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;
            int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
            if (result == ETIMEDOUT || result != 0) {
                PWL_LOG_ERR("Time out to get MD version, retry.");
                err = 1;
            }
            pthread_mutex_unlock(&g_mutex);
            if (err) {
                retry++;
                continue;
            }
            break;
        }
        if (strlen(g_current_md_ver) <= 0) {
            PWL_LOG_ERR("Get MD version error, abort!");
            return RET_FAILED;
        }

        // OP version
        memset(g_current_op_ver, 0, sizeof(g_current_op_ver));
        while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
            err = 0;
            send_message_queue(PWL_CID_GET_OP_VER);
            pthread_mutex_lock(&g_mutex);
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;
            int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
            if (result == ETIMEDOUT || result != 0) {
                PWL_LOG_ERR("Time out to get OP version, retry.");
                err = 1;
            }
            pthread_mutex_unlock(&g_mutex);
            if (err) {
                retry++;
                continue;
            }
            break;
        }
        if (strlen(g_current_op_ver) <= 0) {
            PWL_LOG_ERR("Get OP version error, abort!");
            return RET_FAILED;
        }

        // OEM version
        memset(g_current_oem_ver, 0, sizeof(g_current_oem_ver));
        while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
            err = 0;
            send_message_queue(PWL_CID_GET_OEM_VER);
            pthread_mutex_lock(&g_mutex);
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;
            int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
            if (result == ETIMEDOUT || result != 0) {
                PWL_LOG_ERR("Time out to get OEM version, retry.");
                err = 1;
            }
            pthread_mutex_unlock(&g_mutex);
            if (err) {
                retry++;
                continue;
            }
            break;
        }
        if (strlen(g_current_oem_ver) <= 0) {
            PWL_LOG_ERR("Get OEM version error, abort!");
            return RET_FAILED;
        }

        //DPV version
        memset(g_current_dpv_ver, 0, sizeof(g_current_dpv_ver));
        while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
            err = 0;
            send_message_queue(PWL_CID_GET_DPV_VER);
            pthread_mutex_lock(&g_mutex);
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;
            int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
            if (result == ETIMEDOUT || result != 0) {
                PWL_LOG_ERR("Time out to get DPV version, retry.");
                err = 1;
            }
            pthread_mutex_unlock(&g_mutex);
            if (err) {
                retry++;
                continue;
            }
            break;
        }
        if (strlen(g_current_dpv_ver) <= 0) {
            PWL_LOG_ERR("Get DPV version error, abort!");
            return RET_FAILED;
        }

        // Update esim enable state
        get_esim_enable_state();

        if (DEBUG) {
            PWL_LOG_DEBUG("In get_current_fw_version, get version:");
            PWL_LOG_DEBUG("AP version: %s", g_current_fw_ver);
            PWL_LOG_DEBUG("MD version: %s", g_current_md_ver);
            PWL_LOG_DEBUG("OP version: %s", g_current_op_ver);
            PWL_LOG_DEBUG("OEM version: %s", g_current_oem_ver);
            PWL_LOG_DEBUG("DPV version: %s", g_current_dpv_ver);
            PWL_LOG_DEBUG("ESIM enabled: %d", g_esim_enable);
        }
        return RET_OK;
    }
    return RET_FAILED;
}

gint set_preferred_carrier()
{
    PWL_LOG_DEBUG("Set preferred carrier");
    
    // Get preferred carrier name
    int carrier_index = 0;
    for (int i=0; i < (sizeof(g_preferred_carriers)/sizeof(g_preferred_carriers[0])); i++)
    {
        if (strcasecmp(g_pref_carrier, g_preferred_carriers[i]) == 0)
        {
            // send_message_queue_with_content(PWL_CID_SET_PREF_CARRIER, g_preferred_carriers[i]);
            carrier_index = i;
            break;
        }
    }
    // Set 
    int err, ret, retry = 0;
    gb_set_pref_carrier_ret = FALSE;
    while (retry < PWL_FW_UPDATE_RETRY_LIMIT)
    {
        err = 0;
        send_message_queue_with_content(PWL_CID_SET_PREF_CARRIER, (char *)g_preferred_carriers[carrier_index]);
        pthread_mutex_lock(&g_mutex);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;
        ret = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
        if (ret == ETIMEDOUT || ret != 0) {
            PWL_LOG_ERR("Set preferred carrier error, retry!");
            err = 1;
        }
        pthread_mutex_unlock(&g_mutex);
        if (err || !gb_set_pref_carrier_ret) {
            retry++;
            continue;
        }
        return 0;
    }
    return -1;
}

gint del_tune_code()
{
    PWL_LOG_DEBUG("Del tune code");
    int err, ret, retry = 0;
    gb_del_tune_code_ret = FALSE;

    while (retry < PWL_FW_UPDATE_RETRY_LIMIT)
    {
        err = 0;
        send_message_queue(PWL_CID_DEL_TUNE_CODE);
        pthread_mutex_lock(&g_mutex);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;
        ret = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
        if (ret == ETIMEDOUT || ret != 0) {
            PWL_LOG_ERR("Del tune code error, retry!");
            err = 1;
        }
        pthread_mutex_unlock(&g_mutex);
        if (err || !gb_del_tune_code_ret) {
            retry++;
            continue;
        }
        return 0;
    }
    return -1;
}

int clean_oem_pri_version() {
    int err, ret, retry = 0;
    gb_set_oem_pri_ver_ret = FALSE;

    while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
        PWL_LOG_DEBUG("Clean oem pri version");
        err = 0;
        send_message_queue_with_content(PWL_CID_SET_OEM_PRI_VERSION, "DPV00.00.00.00");

        pthread_mutex_lock(&g_mutex);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;
        ret = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
        if (ret == ETIMEDOUT || ret != 0) {
            PWL_LOG_ERR("Clean oem pri version error, retry!");
            err = 1;
        }
        pthread_mutex_unlock(&g_mutex);
        if (err || !gb_set_oem_pri_ver_ret) {
            retry++;
            continue;
        }
        return RET_OK;
    }
    return RET_FAILED;
}

gint set_oem_pri_version()
{
    int err, ret, retry = 0;
    PWL_LOG_DEBUG("Set oem pri version");
    gb_set_oem_pri_ver_ret = FALSE;

    while (retry < PWL_FW_UPDATE_RETRY_LIMIT)
    {
        err = 0;
        if (strstr(g_device_package_ver, "DPV")) {
            send_message_queue_with_content(PWL_CID_SET_OEM_PRI_VERSION, g_device_package_ver);
        } else {
            PWL_LOG_INFO("Skip oem pri set");
            return 0;
        }

        pthread_mutex_lock(&g_mutex);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;
        ret = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
        if (ret == ETIMEDOUT || ret != 0) {
            PWL_LOG_ERR("Set oem pri version error, retry!");
            err = 1;
        }
        pthread_mutex_unlock(&g_mutex);
        if (err || !gb_set_oem_pri_ver_ret) {
            retry++;
            continue;
        }
        return 0;
    }
    return -1;
}

gint get_ati_info()
{
    PWL_LOG_DEBUG("Get ATI");
    int err, retry = 0;
    while (retry < PWL_FW_UPDATE_RETRY_LIMIT)
    {
        err = 0;
        send_message_queue(PWL_CID_GET_ATI);
        pthread_mutex_lock(&g_mutex);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;
        int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
        if (result == ETIMEDOUT || result != 0) {
            PWL_LOG_ERR("Time out to get ATI, retry");
            err = 1;
        }
        pthread_mutex_unlock(&g_mutex);
        if (err)
        {
            retry++;
            continue;
        }
        return 0;
    }
    return -1;
}

gboolean get_oem_pri_version() {
    int err, retry = 0;
    memset(g_oem_pri_ver, 0, sizeof(g_oem_pri_ver));

    while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
        err = 0;
        send_message_queue(PWL_CID_CHECK_OEM_PRI_VERSION);
        if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
            PWL_LOG_ERR("Time out to get oem pri version, retry");
            err = 1;
        }

        if (err || strlen(g_oem_pri_ver) == 0) {
            retry++;
            continue;
        }
        return TRUE;
    }
    return FALSE;
}

int parse_sku_id(char *response, char *module_sku_id) {
    char temp_resp[PWL_MQ_MAX_RESP] = {0};
    char *token;
    int count = 0;
    strcpy(temp_resp, response);

    token = strtok(temp_resp, ":");
    if (token != NULL) {
        token = strtok(NULL, ":");
        if (token != NULL) {
            trim_string(token);
            strcpy(module_sku_id, token);
        }
    }
}

int get_module_sku_id() {
    int err, retry = 0;
    memset(g_module_sku_id, 0, sizeof(g_module_sku_id));

    while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
        err = 0;
        send_message_queue(PWL_CID_GET_MODULE_SKU_ID);
        pthread_mutex_lock(&g_mutex);
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;

        int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
        if (result == ETIMEDOUT || result != 0) {
            PWL_LOG_ERR("Time out to get pref carrier, retry");
            err = 1;
        }
        pthread_mutex_unlock(&g_mutex);
        if (err || strlen(g_module_sku_id) == 0) {
            retry++;
            continue;
        }
        return RET_OK;
    }
    return RET_FAILED;
}

int get_oempri_reset_state(char *response) {
    char temp_resp[PWL_MQ_MAX_RESP] = {0};
    char *token;
    int count = 0;
    strcpy(temp_resp, response);
    token = strtok(temp_resp, " ");
    if (token != NULL) {
        g_oem_pri_reset_state = atoi(token);
        PWL_LOG_INFO("Oem pri reset state: %d", g_oem_pri_reset_state);
        RET_OK;
    }
    return RET_FAILED;
}

int get_oem_version_from_file(char *oem_file_name, char *oem_version) {
    char filename[strlen(oem_file_name) + 1];
    char sar_tuner_ver[30];
    strcpy(filename, oem_file_name);

    char *p;
    p = strtok(filename, "_");

    while (p != NULL) {
        if (!strstr(p, ".cfw")) {
            strcpy(sar_tuner_ver, p);
            p = strtok(NULL, "_");
        } else {
            break;
        }
    }

    if (sar_tuner_ver[3] == '.') {
        strcpy(oem_version, sar_tuner_ver);
    }
    return 0;
}

void update_esim_enable_state(char *esim_state) {
    if (DEBUG) PWL_LOG_DEBUG("[DPV] ESIM state: %s", esim_state);

    char temp_str[50] = {0};
    char *token;

    memset(temp_str, 0, sizeof(temp_str));
    strcpy(temp_str, esim_state);

    token = strtok(temp_str, " ");
    while (token != NULL) {
        if (strcmp(token, "0") == 0) {
            g_esim_enable = FALSE;
            set_bootup_status_value(ESIM_ENABLE_STATE, 0);
            break;
        }

        if (strcmp(token, "1") == 0) {
            g_esim_enable = TRUE;
            set_bootup_status_value(ESIM_ENABLE_STATE, 1);
            break;
        }
        token = strtok(NULL, " ");
    }
}

int get_esim_enable_state() {
    int err, retry = 0;

    while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
        err = 0;
        send_message_queue(PWL_CID_GET_ESIM_STATE);
        if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
            PWL_LOG_ERR("Time out or error to get esim state, retry");
            err = 1;
        }
        if (err) {
            retry++;
            continue;
        }
        return RET_OK;
    }
    return RET_FAILED;
}

gint setup_download_parameter(fdtl_data_t  *fdtl_data)
{
    g_device_count = 1;
    strcpy(g_diag_modem_port[0], "/dev/cdc-wdm*");
    char *temp_pref_carrier = NULL;

    // Compose image list
    g_image_file_count = 0;
    if (g_only_flash_oem_image == FALSE) {
        // fw
        if (g_has_update_include_fw_img) {
            for (int m = 0; m < MAX_DOWNLOAD_FW_IMAGES; m++) {
                if (strstr(g_image_file_fw_list[m], ".dfw")) {
                    strcpy(g_image_file_list[m], g_image_file_fw_list[m]);
                    g_image_file_count++;
                }
            }
        }

        // carrie pri
        int prefered_index = 0;
        for (int m = 0; m < MAX_DOWNLOAD_PRI_IMAGES; m++) {
            if (strstr(g_image_file_carrier_list[m], ".cfw")) {
                if (strstricase(g_image_file_carrier_list[m], g_pref_carrier))
                    prefered_index = g_image_file_count;
                strcpy(g_image_file_list[g_image_file_count], g_image_file_carrier_list[m]);
                g_image_file_count++;
            }
        }

        // Put prefered carrier image to last one
        temp_pref_carrier = (char *)malloc(strlen(g_image_file_list[prefered_index]) + 1);
        memset(temp_pref_carrier, 0, strlen(g_image_file_list[prefered_index]) + 1);
        strcpy(temp_pref_carrier, g_image_file_list[prefered_index]);
        strcpy(g_image_file_list[prefered_index], g_image_file_list[g_image_file_count - 1]);
        strcpy(g_image_file_list[g_image_file_count - 1], temp_pref_carrier);
    }

    // g_image_file_count++;
    // oem pri
    g_has_update_include_oem_img = FALSE;
    g_oem_sku_id = get_oem_sku_id(g_skuid);
    for (int m = 0; m < MAX_DOWNLOAD_OEM_IMAGES; m++) {
        if (strstr(g_image_file_oem_list[m], g_oem_sku_id)) {
            // Clean oem version first
            if (clean_oem_pri_version() == RET_OK) {
                // Del tune code
                if (del_tune_code() == RET_OK) {
                    strcpy(g_image_file_list[g_image_file_count], g_image_file_oem_list[m]);
                    g_image_file_count++;
                    g_has_update_include_oem_img = TRUE;
                    break;
                } else {
                    PWL_LOG_ERR("Del tune code fail. SKIP flash oem pri image.");
                    g_has_update_include_oem_img = FALSE;
                }
            } else {
                PWL_LOG_ERR("Clean oem version fail. SKIP flash oem pri image.");
                g_has_update_include_oem_img = FALSE;
            }
        }
    }

    if (DEBUG) {
        PWL_LOG_DEBUG("Below image will download to module:");
        for (int m = 0; m < MAX_DOWNLOAD_FILES; m++)
            PWL_LOG_DEBUG("%s", g_image_file_list[m]);
        if (g_image_file_count > 0) {
            PWL_LOG_DEBUG("Total img files: %d", g_image_file_count);
            if (temp_pref_carrier != NULL) {
                free(temp_pref_carrier);
                temp_pref_carrier = NULL;
            }
        } else {
            PWL_LOG_ERR("Not found image, abort!");
            if (temp_pref_carrier != NULL) {
                free(temp_pref_carrier);
                temp_pref_carrier = NULL;
            }
            return -1;
        }
    } else {
        if (g_image_file_count > 0) {
            if (g_only_flash_oem_image) {
                PWL_LOG_DEBUG("============================");
                PWL_LOG_DEBUG("[oem image]: %s", g_image_file_list[g_image_file_count - 1]);
                PWL_LOG_DEBUG("============================");
            } else {
                PWL_LOG_DEBUG("============================");
                PWL_LOG_DEBUG("Total download img files: %d", g_image_file_count);
                PWL_LOG_DEBUG("[FW image]:           %s", g_image_file_list[g_fw_image_file_count - 1]);
                PWL_LOG_DEBUG("[Last carrier image]: %s", temp_pref_carrier);
                PWL_LOG_DEBUG("[oem image]:          %s", g_image_file_list[g_image_file_count - 1]);
                PWL_LOG_DEBUG("============================");
            }
            if (temp_pref_carrier != NULL) {
                free(temp_pref_carrier);
                temp_pref_carrier = NULL;
            }
        } else {
            PWL_LOG_ERR("Not found image, abort!");
            if (temp_pref_carrier != NULL) {
                free(temp_pref_carrier);
                temp_pref_carrier = NULL;
            }
            return -1;
        }
    }

    // Convert oem version to device package version
    if (g_has_update_include_oem_img) {
        memset(g_device_package_ver, 0, DEVICE_PACKAGE_VERSION_LENGTH);
        if (strlen(g_image_file_oem_list[0]) > 0) {
            char oem_version[30];
            char *temp_oem_pri_ver;

            memset(oem_version, 0, sizeof(oem_version));
            get_oem_version_from_file(g_image_file_list[g_image_file_count - 1], oem_version);

            if (DEBUG) {
                PWL_LOG_DEBUG("Convert oem version to device package version");
                PWL_LOG_DEBUG("oem_version: %s", oem_version);
            }

            if (strlen(oem_version) > 0) {
                temp_oem_pri_ver = (char *)malloc(strlen(oem_version));
                memset(temp_oem_pri_ver, 0, strlen(oem_version));
                strcat(g_device_package_ver, "DPV00.");
                char ch;
                for (int i = 0; i < strlen(oem_version); i++) {
                    if (oem_version[i] == '.' || oem_version[i] == '_') {
                        continue;
                    } else {
                        ch = oem_version[i];
                        // printf("%c", ch);
                        strncat(temp_oem_pri_ver, &ch, 1);
                    }
                }
                int shift_index = 0;
                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 2; j++) {
                        ch = temp_oem_pri_ver[shift_index + j];
                        // printf("%c", ch);
                        strncat(g_device_package_ver, &ch, 1);
                        if (j == 1 && i < 2) {
                            ch = '.';
                            strncat(g_device_package_ver, &ch, 1);
                        }
                    }
                    shift_index += 2;
                }
                free(temp_oem_pri_ver);
            } else {
                strcpy(g_device_package_ver, "DPV00.00.00.01");
                PWL_LOG_ERR("Error parsing oem version, set to default");
            }
            PWL_LOG_DEBUG("Device_Package: %s", g_device_package_ver);
        } else {
            PWL_LOG_INFO("No oem image");
        }
    }

    int rtn = -1;
    for (int m = 0; m < MAX_DOWNLOAD_FW_IMAGES; m++) {
        if (strstr(g_image_file_fw_list[m], ".dfw")) {
            rtn = decode_key(g_image_file_fw_list[m]);
            break;
        }
    }

    if( rtn < 0 )
    {
       if( rtn == -1 )            PWL_LOG_DEBUG("\nImage file not existed: %s \n", g_image_file_list[ 0 ] );
       else if( rtn == -2 )       PWL_LOG_DEBUG("\nCreate authentication key error \n" );
       else if( rtn == -3 )       PWL_LOG_DEBUG("\nWrite authentication key error \n" );
       else                       PWL_LOG_DEBUG("\nDecode key process error. \n" );

       return -1;
    }

    strcpy( fdtl_data[0].modem_port, g_diag_modem_port[0] );
    strcpy( fdtl_data[0].g_device_model_name, "" );
    fdtl_data[0].download_process_state = DOWNLOAD_INIT;
    fdtl_data[0].device_idx = 0;
    fdtl_data[0].unlock_key = 0;
    fdtl_data[0].update_oem_pri = 0;
    fdtl_data[0].total_device_count = 1;
    make_prefix_string(&fdtl_data[0], 1);

    return 0;
}

gint prepare_update_images() {
    // Get fw image files in fw folder
    DIR *d;
    struct dirent *dir;
    int img_count = 0;
    int total_image_count = 0;
    d = opendir(IMAGE_FW_FOLDER_PATH);
    char *image_full_file_name;
    int image_full_name_len;
    char temp[MAX_PATH];
    char temp_fw_image_file_list[MAX_DOWNLOAD_FW_IMAGES][MAX_PATH];

    // Reset image file list
    memset(g_image_file_fw_list, 0, sizeof(g_image_file_fw_list));
    memset(g_image_file_carrier_list, 0, sizeof(g_image_file_carrier_list));
    memset(g_image_file_oem_list, 0, sizeof(g_image_file_oem_list));
    memset(g_image_file_list, 0, sizeof(g_image_file_list));
    memset(temp_fw_image_file_list, 0, sizeof(temp_fw_image_file_list));

    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, ".dfw") || strstr(dir->d_name, ".cfw")) {
                if (img_count >= MAX_DOWNLOAD_FW_IMAGES) {
                    PWL_LOG_ERR("The max download fw image files is %d, please check!", MAX_DOWNLOAD_FW_IMAGES);
                    return -1;
                }
                image_full_name_len = strlen(IMAGE_FW_FOLDER_PATH) + strlen(dir->d_name) + 1;
                image_full_file_name = (char *) malloc(image_full_name_len);
                memset(image_full_file_name, '\0', image_full_name_len);
                strcat(image_full_file_name, IMAGE_FW_FOLDER_PATH);
                strcat(image_full_file_name, dir->d_name);
                strcpy(temp_fw_image_file_list[img_count], image_full_file_name);
                free(image_full_file_name);
                image_full_file_name = NULL;
                img_count++;
                g_image_file_count = img_count;
            }
        }
        closedir(d);
    } else {
        // PWL_LOG_ERR("Can't find fw image folder, abort!");
        // return -1;
        PWL_LOG_DEBUG("Can't find fw image folder, skip!");
    }

    if (img_count > 0) {
        total_image_count += img_count;
        g_has_update_include_fw_img = TRUE;
        if (img_count <= MAX_DOWNLOAD_FW_IMAGES)
            g_fw_image_file_count = img_count;
        else
            g_fw_image_file_count = MAX_DOWNLOAD_FW_IMAGES;

        // Sort fw image
        for (int i=2; i <= img_count; i++) {
            for (int j=0; j<=img_count-i; j++) {
                if (strcmp(temp_fw_image_file_list[j], temp_fw_image_file_list[j+1]) > 0) {
                    strcpy(temp, temp_fw_image_file_list[j]);
                    strcpy(temp_fw_image_file_list[j], temp_fw_image_file_list[j+1]);
                    strcpy(temp_fw_image_file_list[j+1], temp);
                }
            }
        }

        if (g_image_file_count <= MAX_DOWNLOAD_FW_IMAGES) {
            for (int m=0; m < MAX_DOWNLOAD_FW_IMAGES; m++)
                strcpy(g_image_file_fw_list[m], temp_fw_image_file_list[m]);
        } else {
            for (int m=0; m < MAX_DOWNLOAD_FW_IMAGES; m++)
                strcpy(g_image_file_fw_list[m], temp_fw_image_file_list[(g_image_file_count - MAX_DOWNLOAD_FW_IMAGES) + m]);
        }
        if (DEBUG) {
            for (int m=0; m<MAX_DOWNLOAD_FW_IMAGES; m++) {
                PWL_LOG_DEBUG("fw img: %s", g_image_file_fw_list[m]);
            }
        }
    }

    // Get carrier_pri image files in carrier_pri folder
    d = opendir(IMAGE_CARRIER_FOLDER_PATH);
    img_count = 0;
    image_full_name_len = 0;
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, ".dfw") || strstr(dir->d_name, ".cfw")) {
                if (img_count >= MAX_DOWNLOAD_PRI_IMAGES) {
                    PWL_LOG_ERR("The max download carrier image files is %d, please check!", MAX_DOWNLOAD_PRI_IMAGES);
                    return -1;
                }
                image_full_name_len = strlen(IMAGE_CARRIER_FOLDER_PATH) + strlen(dir->d_name) + 1;
                image_full_file_name = (char *) malloc(image_full_name_len);
                memset(image_full_file_name, '\0', image_full_name_len);
                strcat(image_full_file_name, IMAGE_CARRIER_FOLDER_PATH);
                strcat(image_full_file_name, dir->d_name);
                strcpy(g_image_file_carrier_list[img_count], image_full_file_name);
                free(image_full_file_name);
                image_full_file_name = NULL;
                img_count++;
                g_image_file_count = img_count;
            }
        }
        closedir(d);
    }
    else {
        PWL_LOG_DEBUG("Can't find carrier_pri image folder, skip!");
    }

    if (img_count > 0) {
        total_image_count += img_count;
        if (DEBUG) {
            for (int m=0; m<g_image_file_count; m++)
                PWL_LOG_DEBUG("carrier img: %s", g_image_file_carrier_list[m]);
        }
    }

    // Get oem_pri image files in oem_pri folder
    d = opendir(IMAGE_OEM_FOLDER_PATH);
    img_count = 0;
    image_full_name_len = 0;
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, ".dfw") || strstr(dir->d_name, ".cfw")) {
                if (img_count >= MAX_DOWNLOAD_OEM_IMAGES) {
                    PWL_LOG_ERR("The max download oem image files is %d, please check!", MAX_DOWNLOAD_OEM_IMAGES);
                    return -1;
                }
                image_full_name_len = strlen(IMAGE_OEM_FOLDER_PATH) + strlen(dir->d_name) + 1;
                image_full_file_name = (char *) malloc(image_full_name_len);
                memset(image_full_file_name, '\0', image_full_name_len);
                strcat(image_full_file_name, IMAGE_OEM_FOLDER_PATH);
                strcat(image_full_file_name, dir->d_name);
                strcpy(g_image_file_oem_list[img_count], image_full_file_name);
                free(image_full_file_name);
                image_full_file_name = NULL;
                img_count++;
                g_image_file_count = img_count;
            }
        }
        closedir(d);
    } else {
        PWL_LOG_DEBUG("Can't find oem pri image folder, skip!");
    }

    if (img_count > 0) {
        total_image_count += img_count;
        if (DEBUG) {
            for (int m=0; m<g_image_file_count; m++)
                PWL_LOG_DEBUG("oem img: %s", g_image_file_oem_list[m]);
        }
    }

    if (total_image_count > 0)
        return 0;
    else {
        PWL_LOG_ERR("Can't find any image file, abort update!");
        return -1;
    }
}

gint compare_main_fw_version()
{
    char *img_ver;
    img_ver = (char *) malloc(FW_VERSION_LENGTH);
    int needUpdate = -1;
    
    memset(img_ver, 0, FW_VERSION_LENGTH);

    for (int i=0; i<g_fw_image_file_count; i++)
    {
        get_image_version(g_image_file_fw_list[i], img_ver);
        if (DEBUG)
        {
            PWL_LOG_DEBUG("target image ver: %s", img_ver);
            PWL_LOG_DEBUG("current image ver: %s", g_current_fw_ver);
            if (strcmp(img_ver, g_current_fw_ver) == 0) 
                PWL_LOG_DEBUG("target = current");
            else if (strcmp(img_ver, g_current_fw_ver) > 0)
                PWL_LOG_DEBUG("target > current");
            else if (strcmp(img_ver, g_current_fw_ver) < 0)
                PWL_LOG_DEBUG("target < current");
        }
        if (strcmp(img_ver, g_current_fw_ver) == 0)
            needUpdate = 0;
        else if (strcmp(img_ver, g_current_fw_ver) > 0)
            needUpdate = 1;
    }
    free(img_ver);
    return needUpdate;
}

int start_update_process(gboolean is_startup)
{
    // Init fw update status file
    if (fw_update_status_init() == 0) {
        // get_fw_update_status_value(FIND_FASTBOOT_RETRY_COUNT, &g_check_fastboot_retry_count);
        // get_fw_update_status_value(WAIT_MODEM_PORT_RETRY_COUNT, &g_wait_modem_port_retry_count);
        // get_fw_update_status_value(WAIT_AT_PORT_RETRY_COUNT, &g_wait_at_port_retry_count);
        get_fw_update_status_value(FW_UPDATE_RETRY_COUNT, &g_fw_update_retry_count);
        get_fw_update_status_value(DO_HW_RESET_COUNT, &g_do_hw_reset_count);
        get_fw_update_status_value(NEED_RETRY_FW_UPDATE, &g_need_retry_fw_update);
    }

    if (g_fw_update_retry_count > FW_UPDATE_RETRY_TH || g_do_hw_reset_count > HW_RESET_RETRY_TH) {
        PWL_LOG_ERR("Reached retry threadshold!!! stop firmware update!!! (%d,%d)", g_fw_update_retry_count, g_do_hw_reset_count);
        return -1;
    }

    // Init progress dialog
    g_progress_percent = 0;
    g_is_get_fw_ver = FALSE;

    // Check AP version before download, TEMP
    while (!g_is_get_fw_ver)
    {
        send_message_queue(PWL_CID_UPDATE_FW_VER);
        sleep(1);
        get_current_fw_version();
        PWL_LOG_DEBUG("get fw ver not ready! Retry after 5 sec");
        sleep(5);
    }
    // Check End

    get_env_variable(env_variable, env_variable_length);

    if (DEBUG) PWL_LOG_DEBUG("%s", set_env_variable);
    strcpy(g_progress_status, "<span font='13'>Downloading ...\\n\\n</span><span foreground='red' font='16'>Do not shut down or restart</span>");
    sprintf(g_progress_command,
            "%szenity --progress --text=\"%s\" --percentage=%d --auto-close --no-cancel --width=600 --title=\"%s\"",
            set_env_variable, g_progress_status, 1, "Modem Update");

    if (g_progress_fp != NULL) {
        pclose(g_progress_fp);
        g_progress_fp = NULL;
    }
    if (!is_startup) g_progress_fp = popen(g_progress_command,"w");

    if (!is_startup) update_progress_dialog(2, "Start update process...", NULL);
    PWL_LOG_INFO("Start update Process...");
    int need_get_preferred_carrier = 0;
    int ret = 0;

    // Enable JP FCC config
    set_fw_update_status_value(JP_FCC_CONFIG_COUNT, 1);
    send_message_queue(PWL_CID_SETUP_JP_FCC_CONFIG);
    if (!cond_wait(&g_mutex, &g_cond, 60)) {
        PWL_LOG_ERR("timed out or error for jpp config, continue...");
    }

    // Get current fw version
    if (get_current_fw_version() != 0)
    {
        PWL_LOG_ERR("Get current FW version error!");
        close_progress_msg_box(CLOSE_TYPE_ERROR);
        return -1;
    }

    // Get OEM PRI version
    if (!get_oem_pri_version()) {
        PWL_LOG_ERR("Get oem pri version error!");
        close_progress_msg_box(CLOSE_TYPE_ERROR);
        return -1;
    }

    // Prepare update images to a list
    g_has_update_include_fw_img = FALSE;
    if (!is_startup) update_progress_dialog(2, "Prepare update images...", NULL);
    if (prepare_update_images() != 0)
    {
        PWL_LOG_ERR("Get update images error!");
        close_progress_msg_box(CLOSE_TYPE_ERROR);
        return -1;
    }

    // === Get Sim or preferred carrier first ===
    if (!is_startup) update_progress_dialog(2, "Get carrier...", NULL);
    if (get_sim_carrier() != 0)
    {
        PWL_LOG_ERR("Get Sim carrier fail, get preferred carrier.");
        need_get_preferred_carrier = 1;
    }
    else
    {
        if (strcasecmp(g_pref_carrier, PWL_UNKNOWN_SIM_CARRIER) == 0)
        {
            PWL_LOG_DEBUG("Sim carrier is unknow, get preferred carrier.");
            need_get_preferred_carrier = 1;
        }
    }

    if (need_get_preferred_carrier)
    {
        if (get_preferred_carrier() != 0)
        {
            PWL_LOG_ERR("Get preferred carrier error!");
            close_progress_msg_box(CLOSE_TYPE_ERROR);
            return -1;
        }
    }
    PWL_LOG_DEBUG("Final switch carrier: %s", g_pref_carrier);

    // === Get SKU id (SSID) ===
    if (!is_startup) update_progress_dialog(2, "Get SKU id", NULL);
    if (get_sku_id() != 0)
    {
        PWL_LOG_ERR("Get SKU ID error!");
        close_progress_msg_box(CLOSE_TYPE_ERROR);
        return -1;
    }
    else
        PWL_LOG_DEBUG("SKU ID: %s", g_skuid);

    // === Compare main fw version ===
    if (!is_startup) update_progress_dialog(2, "Compare fw image version", NULL);
    if (is_startup) g_progress_fp = popen(g_progress_command,"w");

    gboolean up_to_date = FALSE;
    if (g_has_update_include_fw_img) {
        if ((COMPARE_FW_IMAGE_VERSION == 1 && compare_main_fw_version() == 0) ||
            (COMPARE_FW_IMAGE_VERSION == 2 && compare_main_fw_version() <= 0)) {
            up_to_date = TRUE;
        }
    } else {
        PWL_LOG_INFO("Update images don't include any FW image. Continue to compare oem pri version");

        for (int m = 0; m < MAX_DOWNLOAD_OEM_IMAGES; m++) {
            if (strstr(g_image_file_oem_list[m], g_oem_pri_ver)) {
                PWL_LOG_INFO("oem match %s", g_image_file_oem_list[m]);
                up_to_date = TRUE;
                break;
            }
        }
    }

    // Check mdm oem recevery
    g_oem_pri_reset_state = -1;
    if (check_oempri_reset_state() == RET_OK) {
        PWL_LOG_INFO("Oempri reset check pass");
    } else {
        PWL_LOG_ERR("Oempri reset check failed, need flash image.");
        up_to_date = FALSE;
    }

    // Check if module sku match ssid
    g_only_flash_oem_image = FALSE;
    if (up_to_date) {
        if (get_module_sku_id() == RET_OK) {
            // g_skuid: device ssid
            // get_oem_sku_id(g_skuid): convert oem sku id from device ssid
            // g_module_sku_id: module skuid, get from at cmd
            // Compare module sku id

            if (strcmp(g_module_sku_id, get_oem_sku_id(g_skuid)) != 0) {
                PWL_LOG_ERR("Module sku check failed, need flash image.");
                g_only_flash_oem_image = TRUE;
                up_to_date = FALSE;
            } else {
                PWL_LOG_INFO("Module sku check pass");
                g_only_flash_oem_image = FALSE;
                up_to_date = TRUE;
            }
        }
    }

    if (up_to_date) {
        PWL_LOG_INFO("Current fw image already up to date, abort! ");
        if (!is_startup) {
            update_progress_dialog(80, "up to date", "#Modem firmware is up to date\\n\\n\n");
            g_usleep(1000*1000*3);
            update_progress_dialog(100, "Finish update.", "#Modem firmware is up to date\\n\\n\n");
        }
        if (g_progress_fp != NULL) {
            pclose(g_progress_fp);
            g_progress_fp = NULL;
        }
        return -1;
    }

    // === Setup download parameter ===
    fdtl_data_t  fdtl_data[1];
    if (setup_download_parameter(&fdtl_data[0]) != 0)
    {
        PWL_LOG_ERR("setup_download_parameter error");
        g_fw_update_retry_count++;
        set_fw_update_status_value(FW_UPDATE_RETRY_COUNT, g_fw_update_retry_count);
        ret = DOWNLOAD_PARAMETER_PARSING_FAILED;
    } else {
        // === Start Download process ===
        ret = download_process(&fdtl_data[0]);
    }

    // TODO: GPIO RESET
    if (ENABLE_GPIO_RESET)
    {
        gboolean gpio_reset = FALSE;
        if (ret == SWITCHING_TO_DOWNLOAD_MODE_FAILED) {
            PWL_LOG_ERR("Switch to download mode fail, do GPIO reset.");
            gpio_reset = TRUE;
        } else if (ret == DOWNLOAD_PARAMETER_PARSING_FAILED) {
            PWL_LOG_ERR("Download parameter parsing error.");
        } else if (ret <= 0) {
            PWL_LOG_ERR("Download process occure error, do GPIO reset.");
            gpio_reset = TRUE;
        }

        if (ret <= 0) {
            g_progress_percent = 98;
            update_progress_dialog(1, "Reset", "#Modem download error, will automatically retry later\\n\\n\n");
            g_usleep(1000*1000*3);
            update_progress_dialog(1, "Reset", "#Modem download error, will automatically retry later\\n\\n\n");
            if (g_progress_fp != NULL) {
                pclose(g_progress_fp);
                g_progress_fp = NULL;
            }
        }
        if (gpio_reset) {
            set_fw_update_status_value(NEED_RETRY_FW_UPDATE, 1);
            pwl_core_call_gpio_reset_method_sync (gp_proxy, NULL, NULL);
        }

        if (fdtl_data->download_process_state == DOWNLOAD_COMPLETED || ret >= 0) {
            PWL_LOG_DEBUG("Download completed.");
        }
    }

    remove(g_authenticate_key_file_name);
    if (ret >= 0) return 0;
    else return ret;
}

gboolean gdbus_init(void) {
    gboolean b_ret = TRUE;
    GDBusConnection *conn = NULL;
    GError *p_conn_error = NULL;
    GError *p_proxy_error = NULL;

    PWL_LOG_INFO("gdbus_init: Client started.");

    do {
        b_ret = TRUE;
        gp_loop = g_main_loop_new(NULL, FALSE);   /** create main loop, but do not start it.*/

        /** First step: get a connection */
        conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &p_conn_error);

        if (NULL == p_conn_error) {
            /** Second step: try to get a connection to the given bus.*/
            gp_proxy = pwl_core_proxy_new_sync(conn,
                                               G_DBUS_PROXY_FLAGS_NONE,
                                               PWL_GDBUS_NAME,
                                               PWL_GDBUS_OBJ_PATH,
                                               NULL,
                                               &p_proxy_error);
            if (0 == gp_proxy) {
                PWL_LOG_ERR("gdbus_init: Failed to create proxy. Reason: %s.", p_proxy_error->message);
                g_error_free(p_proxy_error);
                b_ret = FALSE;
            }
        } else {
            PWL_LOG_ERR("gdbus_init: Failed to connect to dbus. Reason: %s.", p_conn_error->message);
            g_error_free(p_conn_error);
            b_ret = FALSE;
        }
        sleep(1);
    } while (FALSE == b_ret);

    if (TRUE == b_ret) {
        /** Third step: Attach to dbus signals */
        register_client_signal_handler(gp_proxy);
    }
    return b_ret;
}

gboolean dbus_service_is_ready(void) {
    gchar *owner_name = NULL;
    owner_name = g_dbus_proxy_get_name_owner((GDBusProxy*)gp_proxy);
    if(NULL != owner_name) {
        PWL_LOG_DEBUG("Owner Name: %s", owner_name);
        g_free(owner_name);
        return TRUE;
    } else {
        PWL_LOG_ERR("Owner Name is NULL.");
        sleep(1);
        gdbus_init();
        return FALSE;
    }
}

void remove_flash_data(int type) {
    PWL_LOG_DEBUG("Remove flash data folder");
    switch (type) {
        case UPDATE_TYPE_FULL:
            remove_folder(UPDATE_FW_FOLDER_FILE);
            remove_folder(UPDATE_DEV_FOLDER_FILE);
            break;
        case UPDATE_TYPE_ONLY_FW:
            remove_folder(UPDATE_FW_FOLDER_FILE);
            break;
        case UPDATE_TYPE_ONLY_DEV:
            remove_folder(UPDATE_DEV_FOLDER_FILE);
            break;
        default:
            break;
    }
    // Remvoe fw update status record
    /*
    if (0 == access(FW_UPDATE_STATUS_RECORD, F_OK)) {
        remove(FW_UPDATE_STATUS_RECORD);
    }*/
}

void signal_callback_notice_module_recovery_finish(int type) {
    int already_retry_count;
    if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
        PWL_LOG_DEBUG("[Notice] Receive module recovery completed signal!");
        PWL_LOG_DEBUG("[Notice] type is: %d", type);
        PWL_LOG_DEBUG("[Notice] Sleep 10 secs to wait for pref to update version");
        sleep(10);
        if (check_update_data(type) != RET_OK) {
            PWL_LOG_ERR("Check flash data error, abort.");
            return;
        }
        PWL_LOG_DEBUG("any fw update ongoing? %s", (g_is_fw_update_processing) ? "Yes" : "No");
        if (g_is_fw_update_processing == NOT_IN_FW_UPDATE_PROCESSING) {
            if (start_update_process_pcie(TRUE, type) == RET_OK) {
                // Download success, remove flash data folder
                remove_flash_data(g_update_type);
                close_progress_msg_box(CLOSE_TYPE_SUCCESS);
                pwl_core_call_request_update_fw_version_method(gp_proxy, NULL, NULL, NULL);
                return;
            } else {
                PWL_LOG_DEBUG("Need update? %s", (g_need_update) ? "Yes" : "No");
                if (g_need_update) {
                    close_progress_msg_box(CLOSE_TYPE_RETRY);
                    get_fw_update_status_value(FW_UPDATE_RETRY_COUNT, &already_retry_count);
                    PWL_LOG_DEBUG("[Notice] already_retry_count: %d", already_retry_count);
                    while (already_retry_count < FW_UPDATE_RETRY_TH) {
                        PWL_LOG_DEBUG("[Notice] Retry fw udpate");
                        if (start_update_process_pcie(TRUE, type) == RET_OK) {
                            remove_flash_data(g_update_type);
                            close_progress_msg_box(CLOSE_TYPE_SUCCESS);
                            pwl_core_call_request_update_fw_version_method(gp_proxy, NULL, NULL, NULL);
                            break;
                        } else {
                            get_fw_update_status_value(FW_UPDATE_RETRY_COUNT, &already_retry_count);
                            if (already_retry_count >= FW_UPDATE_RETRY_TH)
                                close_progress_msg_box(CLOSE_TYPE_ERROR);
                        }
                    }
                }
            }
            // Check if need cxp reboot
            if (g_need_cxp_reboot) {
                g_need_cxp_reboot = FALSE;
                if (DEBUG) PWL_LOG_DEBUG("[CXP] Do reboot");
                switch_t7xx_mode(MODE_HW_RESET);
            }
        } else {
            PWL_LOG_DEBUG("Another update processing on going, ignore.");
        }
    }
    return;
}

static gboolean signal_notice_module_recovery_finish_handler(pwlCore *object, int arg_type, gpointer userdata) {
   if (NULL != g_signal_callback.callback_notice_module_recovery_finish) {
       g_signal_callback.callback_notice_module_recovery_finish(arg_type);
   }

   return TRUE;
}

//void signal_callback_retry_fw_update(const gchar* arg) {
//    PWL_LOG_DEBUG("Receive retry fw update request!");
//    start_update_process();
//    return;
//}

//static gboolean signal_get_retry_fw_update_handler(pwlCore *object, const gchar *arg, gpointer userdata) {
//    if (NULL != g_signal_callback.callback_retry_fw_update) {
//        g_signal_callback.callback_retry_fw_update(arg);
//    }
//
//    return TRUE;
//}

static void cb_owner_name_changed_notify(GObject *object, GParamSpec *pspec, gpointer userdata) {
    gchar *pname_owner = NULL;
    pname_owner = g_dbus_proxy_get_name_owner((GDBusProxy*)object);

    if (NULL != pname_owner) {
        PWL_LOG_DEBUG("DBus service is ready!");
        g_free(pname_owner);
    } else {
        PWL_LOG_DEBUG("DBus service is NOT ready!");
        g_free(pname_owner);
    }
}

bool register_client_signal_handler(pwlCore *p_proxy) {
    PWL_LOG_DEBUG("register_client_signal_handler call.");
    g_ret_signal_handler[0] = g_signal_connect(p_proxy, "notify::g-name-owner", G_CALLBACK(cb_owner_name_changed_notify), NULL);
    //g_ret_signal_handler[1] = g_signal_connect(p_proxy, "request-retry-fw-update-signal",
    //                                           G_CALLBACK(signal_get_retry_fw_update_handler), NULL);
    g_ret_signal_handler[1] = g_signal_connect(p_proxy, "notice-module-recovery-finish",
                                              G_CALLBACK(signal_notice_module_recovery_finish_handler), NULL);
    return TRUE;
}

void registerSignalCallback(signal_callback_t *callback) {
    if (NULL != callback) {
        memcpy(&g_signal_callback, callback, sizeof(signal_callback_t));
    } else {
        PWL_LOG_DEBUG("registerSignalCallback: parameter point is NULL");
    }
}

int unzip_flz(char *flz_file, char *unzip_folder) {
    FILE *zip_file = NULL;
    char command[MAX_COMMAND_LEN] = {0};
    int ret = -1;
    zip_file = fopen(flz_file, "r");
    if (zip_file != NULL) {
        if (DEBUG)
            sprintf(command, "unzip -o %s -d %s", flz_file, unzip_folder);
        else
            sprintf(command, "unzip -o -qq %s -d %s", flz_file, unzip_folder);

        ret = system(command);

        fclose(zip_file);
        return RET_OK;
    }
    return RET_FAILED;
}

xmlXPathObjectPtr get_node_set(xmlDocPtr doc, xmlChar *xpath) {
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        PWL_LOG_ERR("xmlXPathNewContext error!");
        return NULL;
    }

    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        PWL_LOG_ERR("xmlXPathEvalExpression error!");
        return NULL;
    }

    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        PWL_LOG_DEBUG("Path empty");
        return NULL;
    }
    return result;
}

int find_image_file_path(char *image_file_name, char *find_prefix, char *image_path) {
    FILE *fp;
    char find_command[MAX_IMG_FILE_NAME_LEN] = {0};
    sprintf(find_command, "find %s -name %s", find_prefix, image_file_name);

    // PWL_LOG_DEBUG("%s", find_command);

    fp = popen(find_command, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("find image path error!");
        return RET_FAILED;
    }
    char buffer[MAX_IMG_FILE_NAME_LEN] = {0};
    char *ret = fgets(buffer, sizeof(buffer), fp);
    pclose(fp);

    buffer[strcspn(buffer, "\n")] = 0;

    if (strlen(buffer) <= 0)
        return RET_FAILED;

    strncpy(image_path, buffer, strlen(buffer));
    return RET_OK;
}

int find_fw_download_image(char *subsysid, char *carrier_id, char *version) {
    xmlDoc *doc;
    // xmlNode *root_element;
    xmlChar *xpath_oemssid = (xmlChar*) "//OEMSSID";
    xmlChar *xpath_carrier = (xmlChar*) "//Carrier";
    xmlChar *xpath_ap_firmware = (xmlChar*) "//APFirmware";
    xmlChar *xpath_md_firmware = (xmlChar*) "//MDFirmware";
    xmlChar *xpath_op_firmware = (xmlChar*) "//OPFirmware";
    xmlChar *xpath_oem_firmware = (xmlChar*) "//OEMFirmware";

    xmlXPathObjectPtr xpath_obj;
    xmlXPathObjectPtr xpath_carrier_obj;
    xmlXPathObjectPtr xpath_ap_firmware_obj;
    xmlXPathObjectPtr xpath_md_firmware_obj;
    xmlXPathObjectPtr xpath_op_firmware_obj;
    xmlXPathObjectPtr xpath_oem_firmware_obj;

    xmlNodeSetPtr nodeset = NULL;
    xmlNodeSetPtr nodeset_carrier = NULL;
    xmlNodeSetPtr nodeset_ap_firmware = NULL;
    xmlNodeSetPtr nodeset_md_firmware = NULL;
    xmlNodeSetPtr nodeset_op_firmware = NULL;
    xmlNodeSetPtr nodeset_oem_firmware = NULL;

    xmlChar *subsysid_value = NULL;
    xmlChar *carrier_id_value = NULL;
    xmlNode *oemssid_node = NULL;
    xmlNode *carrier_node = NULL;
    xmlNode *temp_node = NULL;
    int default_subsysid_index = -1;
    int subsysid_index = -1;
    int default_carrier_index = -1;
    int carrier_index = -1;
    int img_number_count = 0;
    char xml_file[MAX_IMG_FILE_NAME_LEN] = {0};
    char file_folder[10] = {0};
    char temp_find_prefix[MAX_IMG_FILE_NAME_LEN] = {0};
    char temp_file_path[MAX_IMG_FILE_NAME_LEN] = {0};
    char img_file_path[256] = {0};
    char temp_version_info[OTHER_VERSION_LENGTH] = {0};
    char temp_xpath[MAX_IMG_FILE_NAME_LEN] = {0};
    char temp_checksum_value[20] = {0};

    sprintf(xml_file, "%s/%s", UNZIP_FOLDER_FW, "FwPackageInfo.xml");

    doc = xmlReadFile(xml_file, NULL, 0);
    if (doc == NULL) {
        PWL_LOG_ERR("Read xml failed!");
        return RET_FAILED;
    }

    // Check Subsysid
    xpath_obj = get_node_set(doc, xpath_oemssid);
    if (xpath_obj) {
        nodeset = xpath_obj->nodesetval;
        // printf("%s count: %d\n", xpath_oemssid, nodeset->nodeNr);
        for (int i = 0; i < nodeset->nodeNr; i++) {
            subsysid_value = xmlGetProp(nodeset->nodeTab[i], (xmlChar *) "Subsysid");

            if (xmlStrcmp(subsysid_value, "default") == 0) {
                default_subsysid_index = i;
            } else if (xmlStrcmp(subsysid_value, subsysid) == 0) {
                subsysid_index = i;
            }
            xmlFree(subsysid_value);
            if (subsysid_index < 0)
                subsysid_index = default_subsysid_index;
        }
        if (DEBUG) PWL_LOG_DEBUG("Target subsysid index: %d", subsysid_index);

        oemssid_node = nodeset->nodeTab[subsysid_index];
        subsysid_value = xmlGetProp(oemssid_node, (xmlChar *) "Subsysid");
        // strcpy(subsysid_value, xmlGetProp(oemssid_node, "Subsysid"));
        printf("oemssid_node name: %s\n", subsysid_value);

        // Check Carrier id
        sprintf(temp_xpath, "/Package/OEMSSIDList/OEMSSID[@Subsysid='%s']/CarrierList/Carrier", subsysid_value);
        xpath_carrier = (temp_xpath);
        xpath_carrier_obj = get_node_set(oemssid_node->doc, xpath_carrier);

        if (xpath_carrier_obj) {
            nodeset_carrier = xpath_carrier_obj->nodesetval;
            if (DEBUG) PWL_LOG_DEBUG("[Notice] nodeset_carrier->nodeNr: %d", nodeset_carrier->nodeNr);
            for (int i = 0; i < nodeset_carrier->nodeNr; i++) {
                carrier_id_value = xmlGetProp(nodeset_carrier->nodeTab[i], "id");
                if (DEBUG) PWL_LOG_DEBUG("[Notice] carrier_id_value: %s", carrier_id_value);
                if (xmlStrcmp(carrier_id_value, "default") == 0) {
                    default_carrier_index = i;
                } else if (xmlStrcmp(carrier_id_value, carrier_id) == 0) {
                    carrier_index = i;
                }
                xmlFree(carrier_id_value);
                if (carrier_index < 0)
                    carrier_index = default_carrier_index;
            }
            if (DEBUG) {
                PWL_LOG_DEBUG("default_carrier_index: %d", default_carrier_index);
                PWL_LOG_DEBUG("carrier_index: %d", carrier_index);
            }
            carrier_node = nodeset_carrier->nodeTab[carrier_index];
            carrier_id_value = xmlGetProp(carrier_node, (xmlChar *) "id");
        }

        //Find all ap firmware file
        sprintf(temp_xpath, "/Package/OEMSSIDList/OEMSSID[@Subsysid='%s']/CarrierList/Carrier[@id='%s']/APFirmware", subsysid_value, carrier_id_value);
        xpath_ap_firmware = (temp_xpath);
        PWL_LOG_DEBUG("xpath_ap_firmware: %s", xpath_ap_firmware);
        xpath_ap_firmware_obj = get_node_set(oemssid_node->doc, xpath_ap_firmware);

        if (xpath_ap_firmware_obj) {
            nodeset_ap_firmware = xpath_ap_firmware_obj->nodesetval;
            // printf("File: %s, Ver: %s, Type: %s\n", 
            //     xmlGetProp(nodeset_ap_firmware->nodeTab[0], "File"),
            //     xmlGetProp(nodeset_ap_firmware->nodeTab[0], "Ver"),
            //     xmlGetProp(nodeset_ap_firmware->nodeTab[0], "Type"));

            memset(temp_version_info, 0, sizeof(temp_version_info));
            xmlChar *version_node = xmlGetProp(nodeset_ap_firmware->nodeTab[0], "Ver");
            strcpy(temp_version_info, version_node);
            strcpy(version, version_node);
            xmlFree(version_node);

            memset(temp_find_prefix, 0, sizeof(temp_find_prefix));
            xmlChar *file_node = xmlGetProp(nodeset_ap_firmware->nodeTab[0], "File");
            sprintf(temp_find_prefix, "%s/%s*", UNZIP_FOLDER_FW, file_node);
            xmlFree(file_node);

            for (temp_node = nodeset_ap_firmware->nodeTab[0]->children; temp_node; temp_node = temp_node->next) {
                if (temp_node->type == XML_ELEMENT_NODE) {
                    xmlChar *tmp_node = NULL;
                    // PWL_LOG_DEBUG("%s", xmlGetProp(temp_node, "File"));
                    memset(temp_file_path, 0, sizeof(temp_file_path));
                    memset(img_file_path, 0, sizeof(img_file_path));

                    tmp_node = xmlGetProp(temp_node, "File");
                    find_image_file_path(tmp_node, temp_find_prefix, temp_file_path);
                    xmlFree(tmp_node);

                    if (DEBUG) PWL_LOG_DEBUG("current ap: %s, flz ap: %s", g_current_fw_ver, temp_version_info);

                    if (strlen(temp_file_path) > 0) {
                        if (CHECK_CHECKSUM) {
                            memset(temp_checksum_value, 0, sizeof(temp_checksum_value));
                            tmp_node = xmlGetProp(temp_node, "checksum");
                            if (tmp_node) {
                                strcpy(temp_checksum_value, tmp_node);
                                xmlFree(tmp_node);
                            }
                            if (strlen(temp_checksum_value) > 0) {
                                sprintf(img_file_path, "%s|%s", temp_file_path, temp_checksum_value);
                            } else {
                                strcpy(img_file_path, temp_file_path);
                            }
                        } else {
                            strcpy(img_file_path, temp_file_path);
                        }
                        if (CHECK_AP_VERSION) {
                            if (g_update_based_type == PCIE_UPDATE_BASE_FLZ) {
                                if (strncmp(g_current_fw_ver, temp_version_info, strlen(g_current_fw_ver)) == 0) {
                                    sprintf(g_pcie_download_image_list[img_number_count], "SKIP_%s", img_file_path);
                                } else {
                                    strcpy(g_pcie_download_image_list[img_number_count], img_file_path);
                                }
                            } else {
                                strcpy(g_pcie_download_image_list[img_number_count], img_file_path);
                            }
                        } else {
                            strcpy(g_pcie_download_image_list[img_number_count], img_file_path);
                        }
                        img_number_count++;
                    }
                }
            }
        }

        //Find all md firmware file
        sprintf(temp_xpath, "/Package/OEMSSIDList/OEMSSID[@Subsysid='%s']/CarrierList/Carrier[@id='%s']/MDFirmware", subsysid_value, carrier_id_value);
        xpath_md_firmware = (temp_xpath);
        xpath_md_firmware_obj = get_node_set(oemssid_node->doc, xpath_md_firmware);

        if (xpath_md_firmware_obj) {
            nodeset_md_firmware = xpath_md_firmware_obj->nodesetval;
            // printf("File: %s, Ver: %s, Type: %s\n", 
            //     xmlGetProp(nodeset_md_firmware->nodeTab[0], "File"),
            //     xmlGetProp(nodeset_md_firmware->nodeTab[0], "Ver"),
            //     xmlGetProp(nodeset_md_firmware->nodeTab[0], "Type"));
            memset(temp_version_info, 0, sizeof(temp_version_info));
            xmlChar *version_node = xmlGetProp(nodeset_md_firmware->nodeTab[0], "Ver");
            strcpy(temp_version_info, version_node);
            xmlFree(version_node);

            memset(temp_find_prefix, 0, sizeof(temp_find_prefix));
            xmlChar *file_node = xmlGetProp(nodeset_md_firmware->nodeTab[0], "File");
            sprintf(temp_find_prefix, "%s/%s*", UNZIP_FOLDER_FW, file_node);
            xmlFree(file_node);

            for (temp_node = nodeset_md_firmware->nodeTab[0]->children; temp_node; temp_node = temp_node->next) {
                if (temp_node->type == XML_ELEMENT_NODE) {
                    xmlChar *tmp_node = NULL;
                    // PWL_LOG_DEBUG("%s", xmlGetProp(temp_node, "File"));
                    memset(temp_file_path, 0, sizeof(temp_file_path));
                    memset(img_file_path, 0, sizeof(img_file_path));
                    tmp_node = xmlGetProp(temp_node, "File");
                    find_image_file_path(tmp_node, temp_find_prefix, temp_file_path);
                    xmlFree(tmp_node);
                    if (DEBUG) PWL_LOG_DEBUG("current md: %s, flz md: %s", g_current_md_ver, temp_version_info);

                    if (strlen(temp_file_path) > 0) {
                        if (CHECK_CHECKSUM) {
                            memset(temp_checksum_value, 0, sizeof(temp_checksum_value));
                            tmp_node = xmlGetProp(temp_node, "checksum");
                            if (tmp_node) {
                                strcpy(temp_checksum_value, tmp_node);
                                xmlFree(tmp_node);
                            }
                            if (strlen(temp_checksum_value) > 0) {
                                sprintf(img_file_path, "%s|%s", temp_file_path, temp_checksum_value);
                            } else {
                                strcpy(img_file_path, temp_file_path);
                            }
                        } else {
                            strcpy(img_file_path, temp_file_path);
                        }
                        if (CHECK_MD_VERSION) {
                            if (g_update_based_type == PCIE_UPDATE_BASE_FLZ) {
                                if (strncmp(g_current_md_ver, temp_version_info, strlen(g_current_md_ver)) == 0) {
                                    sprintf(g_pcie_download_image_list[img_number_count], "SKIP_%s", img_file_path);
                                } else {
                                    strcpy(g_pcie_download_image_list[img_number_count], img_file_path);
                                }
                            } else {
                                strcpy(g_pcie_download_image_list[img_number_count], img_file_path);
                            }
                        } else {
                            strcpy(g_pcie_download_image_list[img_number_count], img_file_path);
                        }
                        img_number_count++;
                    }
                }
            }
        }

        //Find OP Firmware
        sprintf(temp_xpath, "/Package/OEMSSIDList/OEMSSID[@Subsysid='%s']/CarrierList/Carrier[@id='%s']/OPFirmware", subsysid_value, carrier_id_value);
        xpath_op_firmware = (temp_xpath);
        xpath_op_firmware_obj = get_node_set(oemssid_node->doc, xpath_op_firmware);

        if (xpath_op_firmware_obj) {
            nodeset_op_firmware = xpath_op_firmware_obj->nodesetval;
            temp_node = nodeset_op_firmware->nodeTab[0];
            // printf("%s\n", xmlGetProp(temp_node, "File"));
            memset(temp_version_info, 0, sizeof(temp_version_info));
            xmlChar *version_node = xmlGetProp(temp_node, "Ver");
            sprintf(temp_version_info, "OP.%s", version_node);
            xmlFree(version_node);
            memset(temp_file_path, 0, sizeof(temp_file_path));
            memset(img_file_path, 0, sizeof(img_file_path));
            xmlChar *file_node = xmlGetProp(temp_node, "File");
            find_image_file_path(file_node, UNZIP_FOLDER_FW, temp_file_path);
            xmlFree(file_node);
            if (DEBUG) PWL_LOG_DEBUG("current op: %s, flz op: %s", g_current_op_ver, temp_version_info);

            if (strlen(temp_file_path) > 0) {
                xmlChar *tmp_node = NULL;
                if (CHECK_CHECKSUM) {
                    memset(temp_checksum_value, 0, sizeof(temp_checksum_value));
                    tmp_node = xmlGetProp(temp_node, "checksum");
                    if (tmp_node) {
                        strcpy(temp_checksum_value, tmp_node);
                        xmlFree(tmp_node);
                    }
                    if (strlen(temp_checksum_value) > 0) {
                        sprintf(img_file_path, "%s|%s", temp_file_path, temp_checksum_value);
                    } else {
                        strcpy(img_file_path, temp_file_path);
                    }
                } else {
                    strcpy(img_file_path, temp_file_path);
                }
                if (CHECK_OP_VERSION) {
                    if (g_update_based_type == PCIE_UPDATE_BASE_FLZ) {
                        if (strncmp(g_current_op_ver, temp_version_info, strlen(g_current_op_ver)) == 0) {
                            sprintf(g_pcie_download_image_list[img_number_count], "SKIP_%s", img_file_path);
                        } else {
                            strcpy(g_pcie_download_image_list[img_number_count], img_file_path);
                        }
                    } else {
                        strcpy(g_pcie_download_image_list[img_number_count], img_file_path);
                    }
                } else {
                    strcpy(g_pcie_download_image_list[img_number_count], img_file_path);
                }
                img_number_count++;
            }
        }

        //Find OEM Firmware
        sprintf(temp_xpath, "/Package/OEMSSIDList/OEMSSID[@Subsysid='%s']/CarrierList/Carrier[@id='%s']/OEMFirmware", subsysid_value, carrier_id_value);
        xpath_oem_firmware = (temp_xpath);
        xpath_oem_firmware_obj = get_node_set(oemssid_node->doc, xpath_oem_firmware);

        if (xpath_oem_firmware_obj) {
            nodeset_oem_firmware = xpath_oem_firmware_obj->nodesetval;
            temp_node = nodeset_oem_firmware->nodeTab[0];
            // PWL_LOG_DEBUG("%s", xmlGetProp(temp_node, "File"));
            memset(temp_version_info, 0, sizeof(temp_version_info));
            xmlChar *version_node = xmlGetProp(temp_node, "Ver");
            sprintf(temp_version_info, "OEM.%s", version_node);
            xmlFree(version_node);
            memset(temp_file_path, 0, sizeof(temp_file_path));
            memset(img_file_path, 0, sizeof(img_file_path));
            xmlChar *file_node = xmlGetProp(temp_node, "File");
            find_image_file_path(file_node, UNZIP_FOLDER_FW, temp_file_path);
            xmlFree(file_node);
            if (DEBUG) PWL_LOG_DEBUG("current oem: %s, flz oem: %s", g_current_oem_ver, temp_version_info);

            if (strlen(temp_file_path) > 0) {
                xmlChar *tmp_node = NULL;
                if (CHECK_CHECKSUM) {
                    memset(temp_checksum_value, 0, sizeof(temp_checksum_value));
                    tmp_node = xmlGetProp(temp_node, "checksum");
                    if (tmp_node) {
                        strcpy(temp_checksum_value, tmp_node);
                        xmlFree(tmp_node);
                    }
                    if (strlen(temp_checksum_value) > 0) {
                        sprintf(img_file_path, "%s|%s", temp_file_path, temp_checksum_value);
                    } else {
                        strcpy(img_file_path, temp_file_path);
                    }
                } else {
                    strcpy(img_file_path, temp_file_path);
                }
                if (CHECK_OEM_VERSION) {
                    if (g_update_based_type == PCIE_UPDATE_BASE_FLZ) {
                        if (strncmp(g_current_oem_ver, temp_version_info, strlen(g_current_oem_ver)) == 0) {
                            sprintf(g_pcie_download_image_list[img_number_count], "SKIP_%s", img_file_path);
                        } else {
                            strcpy(g_pcie_download_image_list[img_number_count], img_file_path);
                        }
                    } else {
                        strcpy(g_pcie_download_image_list[img_number_count], img_file_path);
                    }
                } else {
                    strcpy(g_pcie_download_image_list[img_number_count], img_file_path);
                }
                img_number_count++;
            }
        }
    }
    g_pcie_img_number_count = img_number_count;
    xmlFree(subsysid_value);
    xmlFree(carrier_id_value);
    xmlXPathFreeObject(xpath_ap_firmware_obj);
    xmlXPathFreeObject(xpath_md_firmware_obj);
    xmlXPathFreeObject(xpath_op_firmware_obj);
    xmlXPathFreeObject(xpath_oem_firmware_obj);
    xmlXPathFreeObject(xpath_carrier_obj);
    xmlXPathFreeObject(xpath_obj);
    xmlFreeDoc(doc);
    return RET_OK;
}

int find_device_image(char *sku_id) {
    xmlDoc *doc;
    xmlChar *xpath_sku_id = (xmlChar*) "//UniqueSkuID";
    xmlXPathObjectPtr xpath_sku_id_obj;
    xmlNodeSetPtr nodeset_sku_id = NULL;
    xmlNode *temp_node = NULL;

    char xml_file[MAX_IMG_FILE_NAME_LEN] = {0};
    char image_name[MAX_IMG_FILE_NAME_LEN] = {0};
    char image_path[MAX_IMG_FILE_NAME_LEN] = {0};

    sprintf(xml_file, "%s/%s", UNZIP_FOLDER_DVP, "DevMappingTable.xml");
    doc = xmlReadFile(xml_file, NULL, 0);

    if (doc == NULL) {
        PWL_LOG_ERR("Read xml failed!");
        return RET_FAILED;
    }

    xpath_sku_id_obj = get_node_set(doc, xpath_sku_id);
    // PWL_LOG_DEBUG("xpath_sku_id_obj: %s", xpath_sku_id_obj->nodesetval->nodeTab[0]->name);
    nodeset_sku_id = xpath_sku_id_obj->nodesetval;
    // nodeset_ap_firmware->nodeTab[0]->children
    for (temp_node = nodeset_sku_id->nodeTab[0]; temp_node; temp_node = temp_node->next){
        if (temp_node->type == XML_ELEMENT_NODE) {
            xmlChar *prod_node = xmlGetProp(temp_node, "ProductID");
            char prod_id[PWL_MAX_SKUID_SIZE] = {0};
            if (prod_node) {
                int len = (strlen(prod_node) > (PWL_MAX_SKUID_SIZE - 1) ? (PWL_MAX_SKUID_SIZE - 1) : strlen(prod_node));
                strncpy(prod_id, prod_node, len);
            }
            xmlFree(prod_node);

            if (DEBUG) PWL_LOG_DEBUG("[DPV] ProductID: %s", prod_id);

            if (strcmp(sku_id, prod_id) == 0) {
                xmlChar *dev_node = xmlGetProp(temp_node, "DevID");
                xmlChar *esim_node = xmlGetProp(temp_node, "Esim");

                PWL_LOG_DEBUG("[DPV] ESIM: %s, g_esim_enable: %d", esim_node, g_esim_enable);

                if (esim_node == NULL) {
                    PWL_LOG_INFO("[DPV] Can't get esim tag from xml, set as default enable");
                    sprintf(image_name, "%s.img", dev_node);
                    find_image_file_path(image_name, UNZIP_FOLDER_DVP, image_path);
                    xmlFree(dev_node);
                    break;
                }

                if (g_esim_enable == TRUE && strcmp(esim_node, "Enable") == 0) {
                    sprintf(image_name, "%s.img", dev_node);
                    find_image_file_path(image_name, UNZIP_FOLDER_DVP, image_path);
                    PWL_LOG_DEBUG("DPV image: %s", image_path);
                    xmlFree(dev_node);
                    xmlFree(esim_node);
                    break;
                } else if (g_esim_enable == TRUE && strcmp(esim_node, "Disable") == 0) {
                    PWL_LOG_DEBUG("[DPV] ProductID match but Esim is Disable, SKIP");
                    continue;
                } else if (g_esim_enable == FALSE && strcmp(esim_node, "Enable") == 0) {
                    PWL_LOG_DEBUG("[DPV] ProductID match but Esim is Enable, SKIP");
                    continue;
                } else if (g_esim_enable == FALSE && strcmp(esim_node, "Disable") == 0) {
                    sprintf(image_name, "%s.img", dev_node);
                    find_image_file_path(image_name, UNZIP_FOLDER_DVP, image_path);
                    PWL_LOG_DEBUG("DPV image: %s", image_path);
                    xmlFree(dev_node);
                    xmlFree(esim_node);
                    break;
                } else {
                    continue;
                }
            }
        }
    }

    PWL_LOG_DEBUG("[DPV] Verify if DPV image exist.");
    if (strlen(image_path) > 0)
        PWL_LOG_INFO("[DPV] DPV img: %s", image_path);
    else {
        PWL_LOG_ERR("[DPV] Can't find DPV image, abort!");
        xmlXPathFreeObject(xpath_sku_id_obj);
        xmlFreeDoc(doc);
        PWL_LOG_ERR("[DPV] Can't find DPV image, abort!");
        return RET_FAILED;
    }

    memset(g_dpv_image_checksum, 0, sizeof(g_dpv_image_checksum));
    if (CHECK_CHECKSUM) {
        if (parse_checksum(DPV_CHECKSUM_PATH, image_name, g_dpv_image_checksum) != RET_OK) {
            PWL_LOG_ERR("Parse checksum of device package image error!");
            xmlXPathFreeObject(xpath_sku_id_obj);
            xmlFreeDoc(doc);
            return RET_FAILED;
        }
    }

    if (CHECK_DPV_VERSION) {
        if (g_update_based_type == PCIE_UPDATE_BASE_FLZ) {
            if (strncmp(g_current_dpv_ver, image_name, strlen(g_current_dpv_ver)) == 0) {
                sprintf(g_pcie_download_image_list[g_pcie_img_number_count], "SKIP_%s", image_path);
            } else {
                strcpy(g_pcie_download_image_list[g_pcie_img_number_count], image_path);
            }
        } else {
            strcpy(g_pcie_download_image_list[g_pcie_img_number_count], image_path);
        }
    } else {
        strcpy(g_pcie_download_image_list[g_pcie_img_number_count], image_path);
    }
    g_pcie_img_number_count++;

    xmlXPathFreeObject(xpath_sku_id_obj);
    xmlFreeDoc(doc);
    return RET_OK;
}

int generate_download_table(char *xml_file) {
    // Create download table file.
    FILE *fp = fopen(FLASH_TABLE_FILE_NAME, "w");
    if (fp == NULL) {
        PWL_LOG_ERR("Error to create flash_table!");
        return RET_FAILED;
    }
    if (g_update_type == UPDATE_TYPE_ONLY_DEV) {
        if (strstr(g_pcie_download_image_list[0], "DevPackage"))
            fprintf(fp, "Flash|%s|%s|%s\n", "mcf2", g_pcie_download_image_list[0], g_dpv_image_checksum);
        else {
            PWL_LOG_ERR("Can't find DevPackage image, abort!");
            fclose(fp);
            return RET_FAILED;
        }
        fclose(fp);
        return RET_OK;
    } else {
        xmlDoc *doc;
        xmlNode *root_element;
        doc = xmlReadFile(xml_file, NULL, 0);
        if (doc == NULL) {
            PWL_LOG_ERR("Read xml failed!");
            fclose(fp);
            return RET_FAILED;
        }
        root_element = xmlDocGetRootElement(doc);
        // print_element_names(root_element);
        xmlNode *cur_node = NULL;
        xmlNode *child_node = NULL;
        xmlNode *storage_type_node = NULL;
        xmlNode *storage_type_child_node = NULL;
        xmlAttr *attr = NULL;
        xmlChar *value = NULL;
        xmlNode *element = NULL;
        xmlChar *element_value = NULL;
        xmlChar *value_partition_name = NULL;
        xmlChar *value_file_name = NULL;
        xmlChar *value_is_download = NULL;
        char *ret;
        char temp_checksum[MAX_CHECKSUM_LEN] = {0};
        char image_path[MAX_IMG_FILE_NAME_LEN] = {0};
        int flash_count = 0;
        bool is_find_mcf2 = false;

        for (cur_node = root_element; cur_node; cur_node = cur_node->next) {
            // attr = cur_node->properties;
            for (child_node = root_element->children; child_node; child_node = child_node->next) {
                if (child_node->type == XML_ELEMENT_NODE) {
                    // PWL_LOG_DEBUG("node: %s", child_node->name);
                    if (xmlStrcmp(child_node->name, "storage_type") == 0) {
                        storage_type_node = child_node;
                        break;
                    }
                }
            }
        }

        for (storage_type_child_node = storage_type_node->children; storage_type_child_node; storage_type_child_node = storage_type_child_node->next) {
            if (storage_type_child_node->type == XML_ELEMENT_NODE) {
                if (xmlStrcmp(storage_type_child_node->name, "partition_index") == 0) {
                    // Get partition index number
                    attr = storage_type_child_node->properties;
                    //value = xmlGetProp(storage_type_child_node, (xmlChar *)"name");

                    // Get partition name, file name and if download
                    for (element = storage_type_child_node->children; element; element = element->next) {
                        if (element->type == XML_ELEMENT_NODE) {
                            if (xmlStrcmp(element->name, "partition_name") == 0) {
                                value_partition_name = xmlNodeListGetString(element->doc, element->children, 1);
                            } else if (xmlStrcmp(element->name, "file_name") == 0) {
                                value_file_name = xmlNodeListGetString(element->doc, element->children, 1);
                            } else if (xmlStrcmp(element->name, "is_download") == 0) {
                                value_is_download = xmlNodeListGetString(element->doc, element->children, 1);
                            }
                        }
                    }

                    for (int i = 0; i < g_pcie_img_number_count; i++) {
                        ret = strstr(g_pcie_download_image_list[i], value_file_name);
                        // PWL_LOG_DEBUG("ret: %s, strstr: %s, %s", ret, g_pcie_download_image_list[i], value_file_name);
                        if (ret) {
                            flash_count++;
                            // Skip mcf2 partition now, add later.
                            if (strcmp(value_partition_name, "mcf2") == 0)
                                continue;
                            memset(temp_checksum, 0, sizeof(temp_checksum));
                            if (CHECK_CHECKSUM) {
                                if (!strstr(g_pcie_download_image_list[i], "0x")) {
                                    parse_checksum(FW_PACKAGE_CHECKSUM_PATH, value_file_name, temp_checksum);
                                }
                            }
                            if (DEBUG) PWL_LOG_DEBUG("Flash|%s|%s|%s", value_partition_name, g_pcie_download_image_list[i], temp_checksum);
                            fprintf(fp, "Flash|%s|%s|%s\n", value_partition_name, g_pcie_download_image_list[i], temp_checksum);
                        }
                    }
                    g_free(value_partition_name);
                    g_free(value_file_name);
                    g_free(value_is_download);
                }
            }
        }
        // Add Device image to flash list
        if (g_update_type == UPDATE_TYPE_FULL) {
            if (strstr(g_pcie_download_image_list[g_pcie_img_number_count - 1], "DevPackage") != 0) {
                fprintf(fp, "Flash|%s|%s|%s\n", "mcf2", g_pcie_download_image_list[g_pcie_img_number_count - 1], g_dpv_image_checksum);
            }
        }

        PWL_LOG_DEBUG("Flash %d partitions, %d images", flash_count, g_pcie_img_number_count);

        xmlFreeDoc(doc);

        fclose(fp);
        return RET_OK;
    }
}

int check_image_downlaod_table() {
    // Check if the image file name in flasth table can find in scatter.xml
    FILE *fp = NULL;
    char temp_string[MAX_IMG_FILE_NAME_LEN] = {0};
    char temp_list[MAX_DONWLOAD_IMAGES][MAX_IMG_FILE_NAME_LEN];
    int count = 0;
    bool is_find = false;
    g_need_update = false;

    fp = fopen(FLASH_TABLE_FILE_NAME, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("Can't open flash table, check failed!");
        return RET_FAILED;
    }

    while(fscanf(fp, "%s", temp_string) == 1) {
        if (!strstr(temp_string, "SKIP_")) {
            g_need_update = true;
        }
        strcpy(temp_list[count], temp_string);
        count++;
    }
    fclose(fp);

    if(!g_need_update) {
        PWL_LOG_DEBUG("All image up to date, no need to update.");
        return RET_FAILED;
    }

    for (int i = 0; i < g_pcie_img_number_count; i++) {
        is_find = false;
        for (int j = 0; j < count; j++) {
            if (strstr(temp_list[j], g_pcie_download_image_list[i]) != 0) {
                if (DEBUG) PWL_LOG_DEBUG("find: %s, in: %d", g_pcie_download_image_list[i], j);
                is_find = true;
                break;
            }
        }
        if (!is_find) {
            PWL_LOG_ERR("Failed, can't find %s", g_pcie_download_image_list[i]);
            return RET_FAILED;
        }
    }
    return RET_OK;
}

int parse_download_table_and_flash() {
    FILE *fp = NULL;
    char download_string[MAX_COMMAND_LEN] = {0};
    char partition[MAX_IMG_FILE_NAME_LEN] = {0};
    char image[MAX_IMG_FILE_NAME_LEN] = {0};
    char checksum[MAX_CHECKSUM_LEN] = {0};
    char *p;
    int index = 0;

    fp = fopen(FLASH_TABLE_FILE_NAME, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("Can't open download table file.");
        return RET_FAILED;
    }

    while(fscanf(fp, "%s", download_string) == 1) {
        index = 0;
        memset(partition, 0, sizeof(partition));
        memset(image, 0, sizeof(image));
        memset(checksum, 0, sizeof(checksum));

        p = strtok(download_string, "|");
        while(p != NULL) {
            p = strtok(NULL, "|");
            if (p != NULL) {
                index++;
                if (index == INDEX_PARTITION) {
                    strcpy(partition, p);
                } else if (index == INDEX_IMAGE) {
                    strcpy(image, p);
                    if (!CHECK_CHECKSUM)
                        break;
                } else if (index == INDEX_CHECKSUM) {
                    strcpy(checksum, p);
                    checksum[strcspn(checksum, "\n")] = 0;
                }
            }
        }
        if (DEBUG) PWL_LOG_DEBUG("flash %s %s", partition, image);
        update_progress_dialog(40/g_pcie_img_number_count, "Download images...", NULL);

        // Ignore SKIP images
        if (strstr(image, "SKIP_")) {
            PWL_LOG_DEBUG("Skip %s", image);
            continue;
        }
        if (flash_image(partition, image, checksum) != RET_OK) {
            PWL_LOG_ERR("Flash %s %s error!", partition, image);
            fclose(fp);
            return RET_FAILED;
        }
    }
    fclose(fp);
    return RET_OK;
}

int query_t7xx_mode(char *mode) {
    FILE *fp;
    char command[MAX_COMMAND_LEN] = {0};
    sprintf(command, "cat %s", g_t7xx_mode_node);
    fp = popen(command, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("find t7xx_mode error!");
        return RET_FAILED;
    }
    char buffer[MAX_COMMAND_LEN] = {0};
    char *ret = fgets(buffer, sizeof(buffer), fp);
    buffer[strcspn(buffer, "\n")] = 0;
    strcpy(mode, buffer);
    pclose(fp);

    return RET_OK;
}

int find_fastboot_port(char *fastboot_port) {
    FILE *fp;
    fp = popen("find /dev/ -name wwan*fast*", "r");
    if (fp == NULL) {
        PWL_LOG_ERR("find port cmd error!!!");
        return RET_FAILED;
    }

    char buffer[50];
    memset(buffer, 0, sizeof(buffer));
    char *ret = fgets(buffer, sizeof(buffer), fp);
    pclose(fp);
    buffer[strcspn(buffer, "\n")] = 0;

    if (strlen(buffer) <= 0)
        return RET_FAILED;

    strcpy(fastboot_port, buffer);
    return RET_OK;
}

int send_fastboot_command(char *command, char *response) {
    int retry = 0;
    char resp[MAX_COMMAND_LEN] = {0};

    if (strlen(g_pcie_fastboot_port) <= 0)
        find_fastboot_port(g_pcie_fastboot_port);

    int fd = open(g_pcie_fastboot_port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        PWL_LOG_ERR("Error opening serial port");
        return RET_FAILED;
    }
    fd_set rset;
    struct timeval time = {FASTBOOT_CMD_TIMEOUT_SEC, 0};
    FD_ZERO(&rset);
    FD_SET(fd, &rset);
    // Send command to fastboot 
    int bytesWritten = write(fd, command, strlen(command));
    if (DEBUG) PWL_LOG_DEBUG("[send] %s > %s", command, g_pcie_fastboot_port);

    // Read resp from fastboot
    while (select(fd + 1, &rset, NULL, NULL, &time) > 0) {
        memset(resp, 0, sizeof(resp));
        ssize_t len = read(fd, resp, sizeof(resp));
        if (len > 0) {
            resp[strcspn(resp, "\n")] = 0;
            if (DEBUG) PWL_LOG_DEBUG("[read] %s < %s", resp, g_pcie_fastboot_port);
            strcpy(response, resp);
            break;
        }
    }
    int close_ret = close(fd);
    return close_ret;
}

int get_fastboot_resp(char *response) {
    int retry = 0;
    char resp[MAX_COMMAND_LEN] = {0};

    if (strlen(g_pcie_fastboot_port) <= 0)
        find_fastboot_port(g_pcie_fastboot_port);

    int fd = open(g_pcie_fastboot_port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        PWL_LOG_ERR("Error opening serial port");
        return RET_FAILED;
    }
    fd_set rset;
    struct timeval time = {FASTBOOT_CMD_TIMEOUT_SEC, 0};
    FD_ZERO(&rset);
    FD_SET(fd, &rset);

    while (select(fd + 1, &rset, NULL, NULL, &time) > 0) {
        memset(resp, 0, sizeof(resp));
        ssize_t len = read(fd, resp, sizeof(resp));
        // PWL_LOG_DEBUG("[read] len: %ld", len);
        if (len > 0) {
            strcpy(response, resp);
            break;
        } 
    }
    int close_ret = close(fd);
    return close_ret;
}

void *get_send_image_resp_thread_func() {
    char fb_resp[MAX_COMMAND_LEN] = {0};
    while (1) {
        get_fastboot_resp(fb_resp);
        if (strlen(fb_resp) > 0) {
            PWL_LOG_DEBUG("Send image to fastboot result: %s", fb_resp);
            if (strstr(fb_resp, "OKAY")) {
                return (void *) RET_OK;
            } else {
                return (void *) RET_FAILED;
            }
        }
    }
    return (void *) RET_FAILED;
}

int flash_image(char *partition, char *image_file, char *checksum) {
    PWL_LOG_DEBUG("\nFlash image: %s > %s", image_file, partition);

    // Check partition and image file name
    if (strlen(partition) <= 0) {
        PWL_LOG_ERR("Partiton error, please check!");
        return RET_FAILED;
    }
    if (strlen(image_file) <= 0) {
        PWL_LOG_ERR("Image file name error, please check!");
        return RET_FAILED;
    }

    PWL_LOG_DEBUG("\n[Req download]");
    FILE *fp;
    char file_size[9] = {0};
    char fb_command[MAX_COMMAND_LEN] = {0};
    char fb_resp[MAX_COMMAND_LEN] = {0};
    char success_resp[MAX_COMMAND_LEN] = {0};
    int ret = 0;

    // Get image file size and convert to hex
    fp = fopen(image_file, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("open file error");
        return RET_FAILED;
    }

    if (fseek(fp, 0, SEEK_END) < 0) {
        fclose(fp);
        return RET_FAILED;
    }
    long size = ftell(fp);
    fclose(fp);

    sprintf(file_size, "%08x", (unsigned int)size);
    sprintf(success_resp, "DATA%s", file_size);
    if (DEBUG) PWL_LOG_DEBUG("file name: %s, size: 0x%s", image_file, file_size);

    // Get downlaod command
    sprintf(fb_command, "download:%s", file_size);
    sleep(1);
    ret = send_fastboot_command(fb_command, fb_resp);
    PWL_LOG_DEBUG("ret: %d, fb_resp: %s", ret, fb_resp);

    if (strcmp(fb_resp, success_resp) != 0) {
        PWL_LOG_ERR("Req download failed!");
        return RET_FAILED;
    }

    // Send image file to fastboot port
    PWL_LOG_DEBUG("\n[Send image file to fastboot]");
    memset(fb_resp, 0, sizeof(fb_resp));
    memset(fb_command, 0, sizeof(fb_command));
    sprintf(fb_command, "cat %s > %s", image_file, g_pcie_fastboot_port);

    void *send_result = NULL;
    pthread_t get_resp_thread;
    pthread_create(&get_resp_thread, NULL, get_send_image_resp_thread_func, NULL);

    sleep(1);
    FILE *fp2;
    fp2 = popen(fb_command, "w");
    if (fp2 == NULL) {
        PWL_LOG_ERR("Send image to fastboot error!");
        return RET_FAILED;
    }
    pclose(fp2);
    pthread_join(get_resp_thread, &send_result);

    if (send_result != (void *) RET_OK) {
        PWL_LOG_ERR("Send image failed, abort!");
        if (send_result != NULL) {
            send_result = NULL;
            free(send_result);
        }
        return RET_FAILED;
    }

    // Flash to partition
    PWL_LOG_DEBUG("\n[Flash image to parition]");

    sleep(1);
    memset(fb_command, 0, sizeof(fb_command));
    memset(fb_resp, 0, sizeof(fb_resp));
    memset(success_resp, 0, sizeof(success_resp));

    sprintf(fb_command, "flash:%s", partition);
    strcpy(success_resp, "OKAY");

    send_fastboot_command(fb_command, fb_resp);

    PWL_LOG_DEBUG("ret: %d, fb_rest: %s", ret, fb_resp);
    if (strcmp(fb_resp, success_resp) != 0) {
        PWL_LOG_ERR("Flash:[%s] to parition:[%s] failed!", image_file, partition);
        if (strstr(fb_resp, "FAILpartition is not writable")) {
            PWL_LOG_ERR("Ignore B partition flash error.");
        } else {
            PWL_LOG_ERR("Flash error: %s", fb_resp);
        }
        return RET_FAILED;
    }

    // Check partition checksum
    if (CHECK_CHECKSUM) {
        PWL_LOG_DEBUG("\n[Check checksum of parition]");

        sleep(1);
        memset(fb_command, 0, sizeof(fb_command));
        memset(fb_resp, 0, sizeof(fb_resp));
        memset(success_resp, 0, sizeof(success_resp));

        sprintf(fb_command, "check:%s", partition);
        send_fastboot_command(fb_command, fb_resp);

        PWL_LOG_DEBUG("[Notice] ret: %d, fb_rest: %s", ret, fb_resp);
        if (!strstr(fb_resp, "OKAYCRC32:")) {
            PWL_LOG_ERR("CRC response error, abort!");
            return RET_FAILED;
        } else {
            char *p;
            p = strtok(fb_resp, ":");

            while (p != NULL) {
                p = strtok(NULL, ":");
                if (p != NULL) {
                    if (strncmp(checksum, p, strlen(checksum)) != 0) {
                        PWL_LOG_ERR("Image checksum [%s] not match to partition checksum [%s], abort!", checksum, p);
                        return RET_FAILED;
                    } else {
                        PWL_LOG_DEBUG("Checksum check pass!");
                        break;
                    }
                }
            }
        }
    }

    return RET_OK;
}

int switch_t7xx_mode(char *mode) {
    // Find path of t7xx_mode
    FILE *fp;
    char *ret;
    char temp_path[MAX_IMG_FILE_NAME_LEN] = {0};
    char command[MAX_COMMAND_LEN] = {0};
    int continue_success_count = 0;
    if (strlen(g_t7xx_mode_node) <= 0) {
        sprintf(command, "find /sys/ -name %s", T7XX_MODE);
        fp = popen(command, "r");
        if (fp == NULL) {
            PWL_LOG_ERR("find t7xx_mode error!");
            return RET_FAILED;
        }
        char buffer[MAX_IMG_FILE_NAME_LEN] = {0};
        ret = fgets(buffer, sizeof(buffer), fp);
        pclose(fp);

        buffer[strcspn(buffer, "\n")] = 0;

        if (strlen(buffer) <= 0)
            return RET_FAILED;
        strcpy(g_t7xx_mode_node, buffer);
    }

    if (DEBUG) PWL_LOG_DEBUG("T7xx node: %s", g_t7xx_mode_node);

    // Find remove node of device
    if (strlen(g_t7xx_mode_remove_node) <= 0) {
        ret = strstr(g_t7xx_mode_node, T7XX_MODE);
        int index = ret - g_t7xx_mode_node;

        for (int i = 0; i < index; i++)
            temp_path[i] = g_t7xx_mode_node[i];

        strcpy(g_t7xx_mode_remove_node, temp_path);
        strcat(g_t7xx_mode_remove_node, "remove");
    }

    if(DEBUG) PWL_LOG_DEBUG("T7xx remove node: %s", g_t7xx_mode_remove_node);

    // Query t7xx_mode
    char t7xx_mode[30] = {0};
    memset(t7xx_mode, 0, sizeof(t7xx_mode));
    query_t7xx_mode(t7xx_mode);
    PWL_LOG_DEBUG("t7xx_mode state: %s", t7xx_mode);

    if (strcmp(mode, MODE_FASTBOOT_SWITCHING) == 0) {
        if (strcmp(t7xx_mode, "fastboot_download") == 0) {
            PWL_LOG_DEBUG("Device already in fastboot_download mode, ignore.");
            return RET_OK;
        } else if (strcmp(t7xx_mode, "unknow") == 0) {
            PWL_LOG_ERR("Device abnormal, abort switch");
            return RET_FAILED;
        }
    }

    // Set t7xx_mode to fastboot switching or reset
    memset(command, 0, sizeof(command));
    if (strcmp(mode, MODE_FASTBOOT_SWITCHING) == 0) {
        sprintf(command, "echo \"%s\" > %s", MODE_FASTBOOT_SWITCHING, g_t7xx_mode_node);
    } else if (strcmp(mode, MODE_HW_RESET) == 0) {
        sprintf(command, "echo \"%s\" > %s", MODE_HW_RESET, g_t7xx_mode_node);
    } else {
        PWL_LOG_ERR("switch mode not define, abort!");
        return RET_FAILED;
    }

    PWL_LOG_DEBUG("%s", command);

    fp = popen(command, "w");
    if (fp == NULL) {
        PWL_LOG_ERR("Set mode to t7xx_mode failed!");
        return RET_FAILED;
    }
    pclose(fp);

    for (int i = 1; i <= 5; i++) {
        if (DEBUG) PWL_LOG_DEBUG("Sleep %d", i);
        sleep(1);
    }

    memset(t7xx_mode, 0, sizeof(t7xx_mode));
    query_t7xx_mode(t7xx_mode);
    PWL_LOG_DEBUG("t7xx_mode state: %s", t7xx_mode);

    // Remove device from pcie
    PWL_LOG_DEBUG("\nRemove");
    memset(command, 0, sizeof(command));
    sprintf(command, "echo 1 > %s", g_t7xx_mode_remove_node);
    PWL_LOG_DEBUG("%s", command);
    fp = popen(command, "w");
    if (fp == NULL) {
        PWL_LOG_ERR("Remove device failed");
        return RET_FAILED;
    }
    pclose(fp);

    for (int i = 1; i <= 5; i++) {
        if (DEBUG) PWL_LOG_DEBUG("Sleep %d", i);
        sleep(1);
    }

    // Rescan
    PWL_LOG_DEBUG("\nRescan");
    memset(command, 0, sizeof(command));
    sprintf(command, "echo 1 > /sys/bus/pci/rescan");
    fp = popen(command, "w");

    if (fp == NULL) {
        PWL_LOG_ERR("Rescan failed\n");
        return RET_FAILED;
    }
    pclose(fp);

    if (strcmp(mode, MODE_FASTBOOT_SWITCHING) == 0) {
        for (int i = 1; i <= 20; i++) {
            memset(t7xx_mode, 0, sizeof(t7xx_mode));
            query_t7xx_mode(t7xx_mode);
            PWL_LOG_DEBUG("count: %d, t7xx_mode: %s", i, t7xx_mode);
            if (strcmp(t7xx_mode, "fastboot_download") == 0) {
                break;
            }
            sleep(1);
        }
        // Sleep 10 secs to wait fastboot statble
        PWL_LOG_DEBUG("\nSleep 10 secs to wait fastboot statble");
        for (int i = 1; i <= 10; i++) {
            if (DEBUG) PWL_LOG_DEBUG("Sleep %d", i);
            sleep(1);
        }

        // Get device version to check fastboot state, need continue success 10 times
        char resp[MAX_COMMAND_LEN] = {0};
        for (int i = 0; i < GETVER_RETRY_LIMIT; i++) {
            sleep(1);
            memset(resp, 0, sizeof(resp));
            send_fastboot_command("getvar:version", resp);
            PWL_LOG_DEBUG("count: %d, resp: %s", i+1, resp);
            if (strstr(resp, "OKAY")) {
                continue_success_count++;
                PWL_LOG_DEBUG("success count: %d", continue_success_count);
                if (continue_success_count > FB_CMD_CONTINUE_SUCCESS_TH) {
                    return RET_OK;
                }
            } else {
                continue_success_count = 0;
                continue;
            }
        }
        return RET_FAILED;
    } else {
        for (int i = 1; i <= 60; i++) {
            memset(t7xx_mode, 0, sizeof(t7xx_mode));
            query_t7xx_mode(t7xx_mode);
            PWL_LOG_DEBUG("count: %d, t7xx_mode: %s", i, t7xx_mode);
            if (strcmp(t7xx_mode, "ready") == 0) {
                break;
            }
            sleep(1);
        }
        return RET_OK;
    }
    return RET_FAILED;
}

int start_update_process_pcie(gboolean is_startup, int based_type) {
    g_update_based_type = based_type;
    int esim_state = -1;
    // Init fw update status file
    if (fw_update_status_init() == 0) {
        // get_fw_update_status_value(FIND_FASTBOOT_RETRY_COUNT, &g_check_fastboot_retry_count);
        // get_fw_update_status_value(WAIT_MODEM_PORT_RETRY_COUNT, &g_wait_modem_port_retry_count);
        // get_fw_update_status_value(WAIT_AT_PORT_RETRY_COUNT, &g_wait_at_port_retry_count);
        get_fw_update_status_value(FW_UPDATE_RETRY_COUNT, &g_fw_update_retry_count);
        get_fw_update_status_value(DO_HW_RESET_COUNT, &g_do_hw_reset_count);
        get_fw_update_status_value(NEED_RETRY_FW_UPDATE, &g_need_retry_fw_update);
    }
    // Get esim state from file
    get_bootup_status_value(ESIM_ENABLE_STATE, &esim_state);
    if (esim_state == 1)
        g_esim_enable = TRUE;
    else
        g_esim_enable = FALSE;

    if (g_fw_update_retry_count > FW_UPDATE_RETRY_TH || g_do_hw_reset_count > HW_RESET_RETRY_TH) {
        PWL_LOG_ERR("Reached retry threadshold!!! stop firmware update!!! (%d,%d)", g_fw_update_retry_count, g_do_hw_reset_count);
        return RET_FAILED;
    }

    FILE *fp;
    int update_result = RET_FAILED;
    char subsysid[10] = {0};
    char sku_id[PWL_MAX_SKUID_SIZE] = {0};
    char fw_package_ver[30] = {0};
    g_need_update = false;
    // Get subsysid
    get_fwupdate_subsysid(subsysid);
    // Get carrier id
    get_carrier_id();
    get_cxp_reboot_flag();

    // Get SKU id
    pwl_get_skuid(sku_id, PWL_MAX_SKUID_SIZE);
    if (DEBUG) PWL_LOG_DEBUG("subsys id: %s, sku id: %s", subsysid, sku_id);

    if (based_type == PCIE_UPDATE_BASE_FLZ) {
        if (get_current_fw_version() != RET_OK) {
            PWL_LOG_ERR("Get current fw version error, abort!");
            return RET_FAILED;
        }
    }

    PWL_LOG_DEBUG("Current AP version: %s", g_current_fw_ver);
    PWL_LOG_DEBUG("Current MD version: %s", g_current_md_ver);
    PWL_LOG_DEBUG("Current OP version: %s", g_current_op_ver);
    PWL_LOG_DEBUG("Current OEM version: %s", g_current_oem_ver);
    PWL_LOG_DEBUG("Current DPV version: %s", g_current_dpv_ver);

    // Init progress dialog
    g_is_fw_update_processing = IN_FW_UPDATE_PROCESSING;
    g_progress_percent = 0;
    g_is_get_fw_ver = FALSE;
    get_env_variable(env_variable, env_variable_length);

    if (DEBUG) PWL_LOG_DEBUG("%s", set_env_variable);
    strcpy(g_progress_status, "<span font='13'>Downloading ...\\n\\n</span><span foreground='red' font='16'>Do not shut down or restart</span>");
    sprintf(g_progress_command,
            "%szenity --progress --text=\"%s\" --percentage=%d --auto-close --no-cancel --width=600 --title=\"%s\"",
            set_env_variable, g_progress_status, 1, "Modem Update");

    if (g_progress_fp != NULL) {
        pclose(g_progress_fp);
        g_progress_fp = NULL;
    }
    if (!is_startup) g_progress_fp = popen(g_progress_command,"w");

    // if (!is_startup) update_progress_dialog(2, "Start update process...", NULL);
    PWL_LOG_INFO("Start update Process...");

    switch (g_update_type) {
        case UPDATE_TYPE_FULL:
            PWL_LOG_DEBUG("carrier: %s, sku: %s", g_carrier_id, sku_id);
            find_fw_download_image(subsysid, g_carrier_id, fw_package_ver);
            find_device_image(sku_id);
            break;
        case UPDATE_TYPE_ONLY_FW:
            find_fw_download_image(subsysid, g_carrier_id, fw_package_ver);
            break;
        case UPDATE_TYPE_ONLY_DEV:
            find_device_image(sku_id);
            break;
        default:
            PWL_LOG_ERR("Update type unknow, abort!");
            if (g_progress_fp != NULL) {
                pclose(g_progress_fp);
                g_progress_fp = NULL;
            }
            g_is_fw_update_processing = NOT_IN_FW_UPDATE_PROCESSING;
            return RET_FAILED;
            break;
    }

    // if (!is_startup) update_progress_dialog(2, "Prepare update images...", NULL);

    // Parse flash partiton in scatter.xml
    if (generate_download_table(SCATTER_PATH) != RET_OK) {
        PWL_LOG_ERR("Parse partition and image name failed, abort!");
        close_progress_msg_box(CLOSE_TYPE_ERROR);
        g_is_fw_update_processing = NOT_IN_FW_UPDATE_PROCESSING;
        return update_result;
    }

    // if (!is_startup) update_progress_dialog(2, "Checking update images...", NULL);
    // Check if all image can find in scatter.xml
    if (check_image_downlaod_table() != RET_OK) {
        if (g_need_update) {
            PWL_LOG_ERR("Check flash table failed, abort!");
            if (!is_startup) close_progress_msg_box(CLOSE_TYPE_ERROR);
        } else {
            PWL_LOG_ERR("No need to update, abort!");
            remove_flash_data(g_update_type);
            if (!is_startup) close_progress_msg_box(CLOSE_TYPE_SKIP);
        }

        g_is_fw_update_processing = NOT_IN_FW_UPDATE_PROCESSING;
        return update_result;
    }

    if (is_startup) g_progress_fp = popen(g_progress_command, "w");

    // Switch to download mode
    update_progress_dialog(3, "Switch to download mode...", NULL);
    if (switch_t7xx_mode(MODE_FASTBOOT_SWITCHING) != RET_OK) {
        PWL_LOG_ERR("Switch to fastboot mode error!");
        goto DO_RESET;
    }

    // Flash images base on download table
    update_progress_dialog(3, "Start download...", NULL);
    if (parse_download_table_and_flash() != RET_OK) {
        PWL_LOG_ERR("Download image error");
        goto DO_RESET;
    }
    update_result = RET_OK;

DO_RESET:
    g_need_cxp_reboot = FALSE;
    PWL_LOG_DEBUG("[Notice] DO RESET, update_result: %d", update_result);

    update_progress_dialog(20, "Preparing reset...", NULL);
    /*
    int wait_count = 0;
    char t7xx_mode[MAX_COMMAND_LEN] = {0};
    memset(t7xx_mode, 0, sizeof(t7xx_mode));
    query_t7xx_mode(t7xx_mode);

    // Wait for module leave fastboot_download mode.
    PWL_LOG_INFO("Waiting for module leave fastboot_download mode.");
    while(!strstr(t7xx_mode, "unknow")) {
        wait_count++;
        if (wait_count > 20)
            break;
        memset(t7xx_mode, 0, sizeof(t7xx_mode));
        query_t7xx_mode(t7xx_mode);
        if (!strstr(t7xx_mode, "unknow")) {
            sleep(5);
        } else {
            PWL_LOG_DEBUG("Leave waiting!");
            break;
        }
    }

    // sleep(5);
    switch_t7xx_mode(MODE_HW_RESET);
    */
    if (do_fastboot_reboot() != RET_OK) {
        switch_t7xx_mode(MODE_HW_RESET);
    }
    update_progress_dialog(10, "Finish download...", NULL);
    g_is_fw_update_processing = NOT_IN_FW_UPDATE_PROCESSING;
    if (update_result == RET_FAILED) {
        set_fw_update_status_value(FW_UPDATE_RETRY_COUNT, g_fw_update_retry_count);
        g_fw_update_retry_count++;
        return RET_FAILED;
    } else {
        // Wait 5 sec to wait mbim port
        sleep(5);
        gchar port[20];
        memset(port, 0, sizeof(port));
        if (pwl_find_mbim_port(port, sizeof(port))) {
            // Download process pass and find mbim port success
            set_fw_update_status_value(FW_UPDATE_RETRY_COUNT, 0);
            g_fw_update_retry_count = 0;
            return RET_OK;
        } else {
            // Download process pass, but can't find mbim port.
            set_fw_update_status_value(FW_UPDATE_RETRY_COUNT, g_fw_update_retry_count);
            g_fw_update_retry_count++;
            return RET_FAILED;
        }
    }

    return RET_FAILED;
}

int do_fastboot_reboot() {
    FILE *fp;
    char fb_command[MAX_COMMAND_LEN] = {0};
    char fb_resp[MAX_COMMAND_LEN] = {0};
    char t7xx_mode[30] = {0};
    int ret = 0;

    strcpy(fb_command, "reboot");
    ret = send_fastboot_command(fb_command, fb_resp);
    PWL_LOG_DEBUG("ret: %d, fb_resp: %s", ret, fb_resp);

    if (strcmp(fb_resp, "OKAY") == 0) {
        PWL_LOG_DEBUG("Wait 5 secs then check module status.");
        sleep(5);
        for (int i = 0; i < 60; i++) {
            sleep(1);
            memset(t7xx_mode, 0, sizeof(t7xx_mode));
            query_t7xx_mode(t7xx_mode);
            if (DEBUG) PWL_LOG_DEBUG("query count: %d, t7xx_mode: %s", i, t7xx_mode);

            if (strcmp(t7xx_mode, "ready") == 0) {
                PWL_LOG_DEBUG("Module ready");
                return RET_OK;
            }
        }
    }
    return RET_FAILED;
}

int check_update_data(int check_type) {
    int update_type = 0;

    int ret = RET_OK;
    int retry = 0;
    while (retry < PWL_FW_UPDATE_RETRY_LIMIT) {
        ret = RET_OK;
        // Check which type udpate
        if (check_type == TYPE_FLASH_FLZ) {
            if (access(UPDATE_FW_FLZ_FILE, F_OK) == 0 &&
                access(UPDATE_DEV_FLZ_FILE, F_OK) == 0) {
                update_type = UPDATE_TYPE_FULL;
            } else if (access(UPDATE_FW_FLZ_FILE, F_OK) == 0 &&
                       access(UPDATE_DEV_FLZ_FILE, F_OK) != 0) {
                update_type = UPDATE_TYPE_ONLY_FW;
            } else if (access(UPDATE_FW_FLZ_FILE, F_OK) != 0 &&
                       access(UPDATE_DEV_FLZ_FILE, F_OK) == 0) {
                update_type = UPDATE_TYPE_ONLY_DEV;
            } else {
                PWL_LOG_ERR("Failed to parse flz image update type (%d,%d)!!!",
                             access(UPDATE_FW_FLZ_FILE, F_OK),
                             access(UPDATE_DEV_FLZ_FILE, F_OK));
                ret = RET_FAILED;
            }
        } else if (check_type == TYPE_FLASH_FOLDER) {
            if (access(UPDATE_FW_FOLDER_FILE, F_OK) == 0 &&
                access(UPDATE_DEV_FOLDER_FILE, F_OK) == 0) {
                update_type = UPDATE_TYPE_FULL;
            } else if (access(UPDATE_FW_FOLDER_FILE, F_OK) == 0 &&
                       access(UPDATE_DEV_FOLDER_FILE, F_OK) != 0) {
                update_type = UPDATE_TYPE_ONLY_FW;
            } else if (access(UPDATE_FW_FOLDER_FILE, F_OK) != 0 &&
                       access(UPDATE_DEV_FOLDER_FILE, F_OK) == 0) {
                update_type = UPDATE_TYPE_ONLY_DEV;
            } else {
                PWL_LOG_ERR("Failed to parse image folder update type (%d, %d)!!!",
                             access(UPDATE_FW_FOLDER_FILE, F_OK),
                             access(UPDATE_DEV_FOLDER_FILE, F_OK));
                ret = RET_FAILED;
            }
        } else {
            PWL_LOG_ERR("Check type abnormal, abort");
            ret = RET_FAILED;
        }
        if (ret == RET_OK) {
            break;
        } else {
            PWL_LOG_ERR("retry image type check");
            retry++;
            sleep(2);
        }
    }
    if (ret != RET_OK) {
        return RET_FAILED;
    }

    g_update_type = update_type;

    // Unzip flz
    if (check_type == TYPE_FLASH_FLZ) {
        switch (update_type) {
            case UPDATE_TYPE_FULL:
                if (unzip_flz(UPDATE_FW_FLZ_FILE, UNZIP_FOLDER_FW) != RET_OK) {
                    PWL_LOG_ERR("Unzip FwPackage flz failed, abort!");
                    return RET_FAILED;
                }
                if (unzip_flz(UPDATE_DEV_FLZ_FILE, UNZIP_FOLDER_DVP) != RET_OK) {
                    PWL_LOG_ERR("Unzip DevPackage flz failed, abort!");
                    return RET_FAILED;
                }
                break;
            case UPDATE_TYPE_ONLY_FW:
                if (unzip_flz(UPDATE_FW_FLZ_FILE, UNZIP_FOLDER_FW) != RET_OK) {
                    PWL_LOG_ERR("Unzip FwPackage flz failed, abort!");
                    return RET_FAILED;
                }
                break;
            case UPDATE_TYPE_ONLY_DEV:
                if (unzip_flz(UPDATE_DEV_FLZ_FILE, UNZIP_FOLDER_DVP) != RET_OK) {
                    PWL_LOG_ERR("Unzip DevPackage flz failed, abort!");
                    return RET_FAILED;
                }
                break;
            default:
                return RET_FAILED;
                break;
        }
    }

    return RET_OK;
}

int parse_checksum(char *checksum_file, char *key_image, char *checksum_value) {
    xmlDoc *doc;
    xmlChar *xpath_file = (xmlChar*) "//file";
    xmlXPathObjectPtr xpath_file_obj;
    xmlNodeSetPtr nodeset_file = NULL;
    xmlNode *temp_node = NULL;
    bool ret = RET_FAILED;
    doc = xmlReadFile(checksum_file, NULL, 0);
    if (doc == NULL) {
        PWL_LOG_ERR("parse_checksum read xml failed!\n");
        return ret;
    }

    xpath_file_obj = get_node_set(doc, xpath_file);
    nodeset_file = xpath_file_obj->nodesetval;

    for (temp_node = nodeset_file->nodeTab[0]; temp_node; temp_node = temp_node->next) {
        if (temp_node->type == XML_ELEMENT_NODE) {
            xmlChar *name_node = xmlGetProp(temp_node, "name");
            if (strstr(key_image, name_node)) {
                xmlChar *checksum_node = xmlGetProp(temp_node, "checksum");
                if (strlen(checksum_node) > 0) {
                    strcpy(checksum_value, checksum_node);
                    ret = RET_OK;
                } else {
                    PWL_LOG_ERR("%s checksum value is empty!", key_image);
                    ret = RET_FAILED;
                }
                xmlFree(checksum_node);
            }
            xmlFree(name_node);
        }
    }
    xmlXPathFreeObject(xpath_file_obj);
    xmlFreeDoc(doc);
    return ret;
}

gint main( int Argc, char **Argv )
{
    PWL_LOG_INFO("start");

    g_device_type = pwl_get_device_type_await();
    if (g_device_type == PWL_DEVICE_TYPE_UNKNOWN) {
        PWL_LOG_INFO("Unsupported device.");
        return 0;
    }

    pwl_discard_old_messages(PWL_MQ_PATH_FWUPDATE);

    char output_message[1024];

    sprintf( output_message, "\n%s v%s\n\n", APPNAME, VERSION );
    printf_fdtl_s( output_message );

    initialize_global_variable();
    pthread_t msg_queue_thread;
    pthread_create(&msg_queue_thread, NULL, msg_queue_thread_func, NULL);

    pthread_t package_monitor_thread;
    pthread_create(&package_monitor_thread, NULL, monitor_package_func2, NULL);

    pthread_t retry_thread;
    pthread_create(&retry_thread, NULL, monitor_retry_func, NULL);

    // Check if fastboot installed
    if (ENABLE_INSTALLED_FASTBOOT_CHECK)
        g_fb_installed = check_fastboot();

    signal_callback_t signal_callback;
    // signal_callback.callback_retry_fw_update = signal_callback_retry_fw_update;
    signal_callback.callback_notice_module_recovery_finish = signal_callback_notice_module_recovery_finish;
    registerSignalCallback(&signal_callback);

    gdbus_init();
    while(!dbus_service_is_ready());
    PWL_LOG_DEBUG("DBus Service is ready");

    // wait for core & madpt ready before start checking for update
    int need_retry = 0;
    get_fw_update_status_value(NEED_RETRY_FW_UPDATE, &need_retry);
    int delay_time = 60;
    if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
        delay_time = delay_time + PWL_RECOVERY_CHECK_DELAY_SEC;
    }
    if (!cond_wait(&g_madpt_wait_mutex, &g_madpt_wait_cond, delay_time)) {
        PWL_LOG_ERR("timed out or error for madpt start, continue...");
    }
    if (need_retry == 1) {
        PWL_LOG_ERR("Skip firmware file check, wait for update retry");
        goto PREPARE_ERROR;
    }

    //=========
    // TODO: 1. stop modem mgr, 2. check fcc unlock process 3. Remove g_authenticate_key_file_name file
    // Stop modem manager
    // stop_modem_mgr();

    // Get current fw version
    if (get_current_fw_version() != 0)
    {
        PWL_LOG_ERR("Get current FW version error!");
        if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
            PWL_LOG_DEBUG("Request pwl_pref to update fw version!");
            pwl_core_call_request_update_fw_version_method(gp_proxy, NULL, NULL, NULL);
        }
        goto PREPARE_ERROR;
    }

    // Check if UPDATE_FW_ZIP_FILE exist
    if (g_device_type == PWL_DEVICE_TYPE_USB) {
        if (access(UPDATE_FW_ZIP_FILE, F_OK) != 0) {
            PWL_LOG_DEBUG("Update fw zip not exist, abort!");
            goto PREPARE_ERROR;
        } else {
            if (extract_update_files() == 0) {
                PWL_LOG_DEBUG("Start update process");
                if (start_update_process(TRUE) != 0) {
                    goto PREPARE_ERROR;
                }
            } else {
                PWL_LOG_ERR("Extra update files error, abort!");
                goto PREPARE_ERROR;
            }
        }
    } else if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
        // Init fw update status file
        if (fw_update_status_init() == 0) {
            get_fw_update_status_value(FW_UPDATE_RETRY_COUNT, &g_fw_update_retry_count);
            get_fw_update_status_value(NEED_RETRY_FW_UPDATE, &g_need_retry_fw_update);
        }
        PWL_LOG_DEBUG("PCIE device, wait pwl_core module recovery check finish.");
    } else {
        goto PREPARE_ERROR;
    }

PREPARE_ERROR:
    if (gp_loop != NULL) {
        PWL_LOG_DEBUG("g_main_loop_run");
        g_main_loop_run(gp_loop);
    }
    if (0 != gp_loop) {
        g_main_loop_quit(gp_loop);
        g_main_loop_unref(gp_loop);
    }
    pthread_join(msg_queue_thread, NULL);
    pthread_join(package_monitor_thread, NULL);
    return 0;
}
