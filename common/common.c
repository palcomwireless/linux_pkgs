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

#include <mqueue.h>
#include <stdio.h>

#include "common.h"
#include "log.h"

static pwl_device_type_t g_device_type = PWL_DEVICE_TYPE_UNKNOWN;
static char *g_subsysid;

char *usb_devices[] = { "0CBD", "0CC1", "0CC4", "0CB5", "0CB7",
                        "0CB2", "0CB3", "0CB4", "0CB9", "0CBA",
                        "0CBB", "0CBC", "0CD9", "0CDA", "0CF4",
                        "0CE8", "0CF9", "0CF5", "0CF6", "0D5F",
                        "0D60", "0D5C", "0D5E", "0D47", "0D48",
                        "0D49", "0D61", "0D4D", "0D4E", "0D4F",
                        "0D65" };

char *pcie_devices[] = { "0CF4", "0CF5", "0CDD", "0CF1", "0CDB" };

gchar* usbid_info[] = {
    "413c:8217",
    "413c:8218",
    "413c:8219",
};
#define USBID_LIST_COUNT        (sizeof(usbid_info) / sizeof(usbid_info[0]))

gchar* pcieid_info[] = {
    "14c0:4d75",
    "14c0:0b5e",
    "14c0:0b63",
    "14c0:0b62",
    "14c0:0b68",
    "14C0:0B68",
    "14c0:0b64",
    "14c0:0b66",
    "14c0:0b65",
    "14c0:0b5f",
};
#define PCIEID_LIST_COUNT       (sizeof(pcieid_info) / sizeof(pcieid_info[0]))

#define SUBSYS_ID_QUERY_CMD "udevadm info --query=all --path=/sys/bus/pci/devices/%s | grep PCI_SUBSYS_ID="
#define SUBSYS "1028:5933"


gboolean pwl_discard_old_messages(const gchar *path) {
    mqd_t mq;
    struct mq_attr attr;
    msg_buffer_t message;
    unsigned int msg_prio;
    int count = 0;

    // Open the message queue
    mq = mq_open(path, O_RDONLY | O_NONBLOCK);
    if (mq == (mqd_t) - 1) {
        PWL_LOG_ERR("mq_open");
        return 0;
    }

    // Get the message queue attributes
    mq_getattr(mq, &attr);
    
    // Receive and discard old messages
    while (mq_receive(mq, (gchar *)&message, sizeof(message), &msg_prio) > 0) {
        count++;
    }

    PWL_LOG_INFO("Discarded %d messages", count);

    // Close the message queue
    mq_close(mq);

    return 0;
}

gboolean get_host_info(const gchar *cmd, gchar *buff, gint buff_len) {
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("cmd error!!!");
        return FALSE;
    }

    if (fgets(buff, buff_len, fp) != NULL) {
        buff[strcspn(buff, "\n")] = 0;
    }

    pclose(fp);

    return TRUE;
}

gboolean filter_host_info_header(const gchar *header, gchar *info, gchar *buff, gint buff_len) {
    gchar* head = strstr(info, header);

    if (head != NULL) {
        gint info_len = strlen(head + strlen(header));

        if (info_len > 1 && buff_len >= (info_len + 1)) {
            strcpy(buff, head + strlen(header));
            return TRUE;
        } else {
            PWL_LOG_ERR("host info buffer error, %d, %d, %s", info_len, buff_len, info);
            return FALSE;
        }
    }
    return FALSE;
}

void pwl_get_manufacturer(gchar *buff, gint buff_len) {
    memset(buff, 0, buff_len);

    gchar manufacturer[INFO_BUFFER_SIZE];
    if (!get_host_info("dmidecode -t 1 | grep 'Manufacturer:'", manufacturer, INFO_BUFFER_SIZE)) {
        PWL_LOG_ERR("Get Manufacturer failed!");
    } else {
        if (!filter_host_info_header("Manufacturer: ", manufacturer, buff, buff_len)) {
            PWL_LOG_ERR("Get Manufacturer info failed!");
        }
    }
}

void pwl_get_skuid(gchar *buff, gint buff_len) {
    memset(buff, 0, buff_len);

    gchar skuid[INFO_BUFFER_SIZE];
    if (!get_host_info("dmidecode -t 1 | grep 'SKU Number:'", skuid, INFO_BUFFER_SIZE)) {
        PWL_LOG_ERR("Get SKU Number failed!");
    } else {
        if (!filter_host_info_header("SKU Number: ", skuid, buff, buff_len)) {
            PWL_LOG_ERR("Get SKU Number info failed!");
        }
    }
}

int get_fwupdate_subsysid(char *subsysid) {
    if (g_subsysid == NULL) {
        PWL_LOG_ERR("SUBSYS error");
        return RET_FAILED;
    }
    char *token;
    char vid[5] = {0};
    char pid[5] = {0};
    char id_buffer[20] = {0};
    strcpy(id_buffer, g_subsysid);
    if (DEBUG) PWL_LOG_DEBUG("[Notice] id_buffer: %s", id_buffer);

    token = strtok(id_buffer, ":");
    if (token == NULL) {
        if (DEBUG) PWL_LOG_ERR("[Notice] Split token is NULL, id_buffer: %s", id_buffer);
        strcpy(subsysid, "default");
        return RET_OK;
    }
    if (strlen(token) > 0) 
        strncpy(vid, token, 4);
    if (DEBUG) PWL_LOG_DEBUG("[Notice] id_buffer: %s, vid: %s", id_buffer, vid);
    while(token != NULL) {
        token = strtok(NULL, ":");
        if (token != NULL) {
            if (strlen(token) > 0) {
                strncpy(pid, token, 4);
                break;
            }
        } else {
            break;
        }
    }
    if (DEBUG) PWL_LOG_DEBUG("[Notice] id_buffer: %s, pid: %s", id_buffer, pid);
    sprintf(subsysid, "%s%s", pid, vid);
    return RET_OK;
}

gboolean pwl_module_device_id_exist(pwl_device_type_t type, gchar *id) {

    gchar *command = NULL;
    if (type == PWL_DEVICE_TYPE_USB)
        command = "lsusb | grep ";
    else
        command = "lspci -D -n | grep ";

    gchar cmd[strlen(command) + strlen(id) + 1];
    memset(cmd, 0, sizeof(cmd));
    sprintf(cmd, "%s%s", command, id);

    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("device id check cmd error!!!");
        return FALSE;
    }

    char response[200];
    memset(response, 0, sizeof(response));
    char *ret = fgets(response, sizeof(response), fp);

    pclose(fp);

    if (ret != NULL && strlen(response) > 0) {
        if (type == PWL_DEVICE_TYPE_USB) {
            return TRUE;
        } else {
            // check pcie subsys id
            const char s[2] = " ";
            char *domain;
            domain = strtok(response, s);
            if (!domain) return FALSE;

            char query_cmd[strlen(SUBSYS_ID_QUERY_CMD) + 20];
            memset(query_cmd, 0, sizeof(query_cmd));
            sprintf(query_cmd, SUBSYS_ID_QUERY_CMD, domain);

            fp = popen(query_cmd, "r");
            if (fp == NULL) {
                PWL_LOG_ERR("device subsys check cmd error!!!");
                return FALSE;
            }

            char subsysid[100];
            memset(subsysid, 0, sizeof(subsysid));
            char *ret = fgets(subsysid, sizeof(subsysid), fp);
            pclose(fp);

            if (strlen(subsysid) > 0) {
                char* subsys = strstr(subsysid, (char *)"PCI_SUBSYS_ID=");
                id = subsys + strlen((char *)"PCI_SUBSYS_ID=");
                if (DEBUG) PWL_LOG_DEBUG("[Notice] id: %s", id);
                if (strncmp(id, SUBSYS, strlen(SUBSYS)) == 0) {
                    if (g_subsysid == NULL) {
                        g_subsysid = malloc(20);
                        memset(g_subsysid, 0, 20);
                        strcpy(g_subsysid, id);
                        if (DEBUG) PWL_LOG_DEBUG("[Notice] g_subsysid: %s", g_subsysid);
                    }
                    return TRUE;
                }
            }

            return FALSE;
        }
    }

    return FALSE;
}

pwl_device_type_t pwl_get_device_type() {
    gchar skuid[PWL_MAX_SKUID_SIZE];

    if (g_device_type != PWL_DEVICE_TYPE_UNKNOWN)
        return g_device_type;

    pwl_get_skuid(skuid, PWL_MAX_SKUID_SIZE);
    if (DEBUG) PWL_LOG_DEBUG("SKU Number: %s", skuid);

    size_t usb_count = sizeof(usb_devices) / sizeof(usb_devices[0]);
    for (int i = 0; i < usb_count; i++) {
        if (strncmp(skuid, usb_devices[i], strlen(usb_devices[i])) == 0) {
            for (gint j = 0; j < USBID_LIST_COUNT; j++) {
                if (pwl_module_device_id_exist(PWL_DEVICE_TYPE_USB, usbid_info[j])) {
                    PWL_LOG_INFO("Device type usb");
                    g_device_type = PWL_DEVICE_TYPE_USB;
                    return PWL_DEVICE_TYPE_USB;
                }
            }
        }
    }

    size_t pcie_count = sizeof(pcie_devices) / sizeof(pcie_devices[0]);
    for (int i = 0; i < pcie_count; i++) {
        if (strncmp(skuid, pcie_devices[i], strlen(pcie_devices[i])) == 0) {
            for (gint j = 0; j < PCIEID_LIST_COUNT; j++) {
                if (pwl_module_device_id_exist(PWL_DEVICE_TYPE_PCIE, pcieid_info[j])) {
                    PWL_LOG_INFO("Device type pcie");
                    g_device_type = PWL_DEVICE_TYPE_PCIE;
                    return PWL_DEVICE_TYPE_PCIE;
                }
            }
        }
    }

    if (DEBUG) PWL_LOG_INFO("Device type unknown");
    return PWL_DEVICE_TYPE_UNKNOWN;
}

pwl_device_type_t pwl_get_device_type_await() {
    pwl_device_type_t type = PWL_DEVICE_TYPE_UNKNOWN;

    for (gint i = 0; i < 30; i++) {
        type = pwl_get_device_type();
        if (type != PWL_DEVICE_TYPE_UNKNOWN)
            return type;
        g_usleep(1000*1000*2);
    }
    return PWL_DEVICE_TYPE_UNKNOWN;
}

gboolean cond_wait(pthread_mutex_t *mutex, pthread_cond_t *cond, gint wait_time) {
    pthread_mutex_unlock(mutex);
    pthread_mutex_lock(mutex);
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += wait_time;

    int result = pthread_cond_timedwait(cond, mutex, &timeout);
    if (result == ETIMEDOUT || result != 0) {
        if (DEBUG) PWL_LOG_ERR("timed out or error!!!");
        return FALSE;
    }

    pthread_mutex_unlock(mutex);

    return TRUE;
}

void send_message_reply(uint32_t cid,
                        uint32_t sender_id,
                        uint32_t dest_id,
                        pwl_cid_status_t status,
                        char *msg) {
    mqd_t mq;
    mq = mq_open(PWL_MQ_PATH(dest_id), O_WRONLY);

    // message to be sent
    msg_buffer_t message;
    message.pwl_cid = cid;
    message.sender_id = sender_id;
    message.status = status;
    strcpy(message.response, msg);

    if (DEBUG) {
        PWL_LOG_DEBUG("Sending reply msg to %s for cid (%d) %s, status %s (%s)",
                      PWL_MQ_PATH(dest_id),
                      message.pwl_cid, cid_name[message.pwl_cid],
                      cid_status_name[message.status],
                      message.response);
    } else {
        PWL_LOG_DEBUG("Sending reply msg to %s for cid (%d) %s, status %s",
                      PWL_MQ_PATH(dest_id),
                      message.pwl_cid, cid_name[message.pwl_cid],
                      cid_status_name[message.status]);
    }

    // msgsnd to send message
    mq_send(mq, (gchar *)&message, sizeof(message), 0);
}

void print_message_info(msg_buffer_t* message) {
    if (DEBUG) {
        PWL_LOG_DEBUG("Msg recv from %s for cid (%d) %s, status %s (%s)",
                      PWL_MQ_PATH(message->sender_id),
                      message->pwl_cid, cid_name[message->pwl_cid],
                      cid_status_name[message->status],
                      (message->status == PWL_CID_STATUS_NONE) ? "" : message->response);
    } else {
        PWL_LOG_DEBUG("Msg recv from %s for cid (%d) %s, status %s",
                      PWL_MQ_PATH(message->sender_id),
                      message->pwl_cid, cid_name[message->pwl_cid],
                      cid_status_name[message->status]);
    }
}

gboolean pwl_find_mbim_port(gchar *port_buff_ptr, guint32 port_buff_size) {

    FILE *fp = NULL;
    pwl_device_type_t type = pwl_get_device_type();
    if (type == PWL_DEVICE_TYPE_USB) {
        fp = popen("find /dev/ -name cdc-wdm*", "r");
    } else if (type == PWL_DEVICE_TYPE_PCIE) {
        fp = popen("find /dev/ -name wwan0mbim*", "r");
    }

    if (fp == NULL) {
        if (DEBUG) PWL_LOG_ERR("find port cmd error!!!");
        return FALSE;
    }

    char buffer[50];
    memset(buffer, 0, sizeof(buffer));
    char *ret = fgets(buffer, sizeof(buffer), fp);
    pclose(fp);

    buffer[strcspn(buffer, "\n")] = 0;

    if ((strlen(buffer) + 1) > port_buff_size) {
        PWL_LOG_ERR("port buffer size %d not enough!!!", port_buff_size);
        return FALSE;
    }

    if (strlen(buffer) <= 0)
        return FALSE;

    strncpy(port_buff_ptr, buffer, strlen(buffer));

    return TRUE;
}

#define PWL_CMD_SET     "mbimcli -d %s -p --compal-query-at-command=\"%s\""

gboolean pwl_set_command(const gchar *command, gchar **response) {
    if (DEBUG) PWL_LOG_DEBUG("Set mbim command: %s", command);

    gchar port[20];
    memset(port, 0, sizeof(port));
    if (!pwl_find_mbim_port(port, sizeof(port))) {
        PWL_LOG_ERR("pwl find mbim port fail!");
        return FALSE;
    }

    gchar cmd[strlen(PWL_CMD_SET) + strlen(port) + strlen(command) + 1];
    memset(cmd, 0, sizeof(cmd));
    sprintf(cmd, PWL_CMD_SET, port, command);
    if (DEBUG) PWL_LOG_DEBUG("%s", cmd);

    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("Set cmd error!!!");
        return FALSE;
    }

    gchar buff[PWL_MQ_MAX_RESP];
    memset(buff, 0, sizeof(buff));
    gchar line[100];
    memset(line, 0, sizeof(line));

    while (fgets(line, sizeof(line), fp) != NULL) {
        //if (DEBUG) PWL_LOG_DEBUG("query resp: %s", line);
        strcat(buff, line);
    }
    pclose(fp);

    if (DEBUG) PWL_LOG_DEBUG("Response(%ld): %s", strlen(buff), buff);

    if (strlen(buff) > 0) {
        *response = (gchar *)malloc(strlen(buff) + 1);
        if (*response == NULL) {
            PWL_LOG_ERR("Command response malloc failed!!");
            return FALSE;
        }
        memset(*response, 0, strlen(buff) + 1);
        memcpy(*response, buff, strlen(buff));
    } else {
        return FALSE;
    }

    return TRUE;
}

gboolean pwl_set_command_available() {
    gboolean available = FALSE;

    gchar *response = NULL;
    if (pwl_set_command("AT", &response)) {
        if (response != NULL && strstr(response, "OK") != NULL) {
            available = TRUE;
        }
    } else {
        return available;
    }
    if (response) {
        free(response);
    }

    return available;
}

int fw_update_status_init() {
    FILE *fp = NULL;

    if (0 == access(FW_UPDATE_STATUS_RECORD, F_OK)) {
        PWL_LOG_DEBUG("File exist");
        fp = fopen(FW_UPDATE_STATUS_RECORD, "r");

        if (fp == NULL) {
            PWL_LOG_ERR("Open fw_status file error!");
            return -1;
        }
        /*
        get_fw_update_status_value(FIND_FASTBOOT_RETRY_COUNT, &g_check_fastboot_retry_count);
        get_fw_update_status_value(WAIT_MODEM_PORT_RETRY_COUNT, &g_wait_modem_port_retry_count);
        get_fw_update_status_value(WAIT_AT_PORT_RETRY_COUNT, &g_wait_at_port_retry_count);
        get_fw_update_status_value(FW_UPDATE_RETRY_COUNT, &g_fw_update_retry_count);
        get_fw_update_status_value(DO_HW_RESET_COUNT, &g_do_hw_reset_count);
        get_fw_update_status_value(NEED_RETRY_FW_UPDATE, &g_need_retry_fw_update);
        */
        fclose(fp);
    } else {
        PWL_LOG_DEBUG("File not exist");
        fp = fopen(FW_UPDATE_STATUS_RECORD, "w");

        if (fp == NULL) {
            PWL_LOG_ERR("Create fw_status file error!");
            return -1;
        }

        fprintf(fp, "Find_fastboot_retry_count=0\n");
        fprintf(fp, "Wait_modem_port_retry_count=0\n");
        fprintf(fp, "Wait_at_port_retry_count=0\n");
        fprintf(fp, "Fw_update_retry_count=0\n");
        fprintf(fp, "Do_hw_reset_count=0\n");
        fprintf(fp, "Need_retry_fw_update=0\n");
        fprintf(fp, "jp_fcc_config_count=0\n");
        fclose(fp);
    }
    return 0;
}

int set_fw_update_status_value(char *key, int value) {
    FILE *fp = NULL;
    char line[STATUS_LINE_LENGTH];
    char new_value_line[STATUS_LINE_LENGTH];
    char *new_content = NULL;

    fp = fopen(FW_UPDATE_STATUS_RECORD, "r+");
    int value_len, file_size, new_file_size;
    value_len = count_int_length(value);
    if (fp == NULL) {
        PWL_LOG_ERR("Open fw_status file error!");
        return -1;
    }

    // Check size
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);

    new_file_size = file_size + value_len + 1;
    new_content = (char *) malloc(new_file_size);
    memset(new_content, 0, new_file_size);

    fseek(fp, 0, SEEK_SET);
    while (fgets(line, STATUS_LINE_LENGTH, fp) != NULL) {
        if (strstr(line, key)) {
            sprintf(new_value_line, "%s=%d\n", key, value);
            strcat(new_content, new_value_line);
        } else {
            strcat(new_content, line);
        }
    }
    fclose(fp);
    fp = fopen(FW_UPDATE_STATUS_RECORD, "w");

    if (fp == NULL) {
        PWL_LOG_ERR("Create fw_status file error!\n");
        return -1;
    }
    fwrite(new_content, sizeof(char), strlen(new_content), fp);
    free(new_content);
    fclose(fp);

    return 0;
}

int get_fw_update_status_value(char *key, int *result) {
    FILE *fp = NULL;
    char line[STATUS_LINE_LENGTH];
    char *temp_pos = NULL;
    char value[5];

    fp = fopen(FW_UPDATE_STATUS_RECORD, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("Open fw_status file error!");
        return -1;
    }

    while (fgets(line, STATUS_LINE_LENGTH, fp) != NULL) {
        if (strstr(line, key)) {
            temp_pos = strchr(line, '=');
            ++temp_pos;
            strncpy(value, temp_pos, strlen(temp_pos));
            if (NULL != strstr(value, "\r\n"))
            {
                PWL_LOG_DEBUG("get result string has carriage return\n");
                value[strlen(temp_pos)-2] = '\0';
            } else {
                value[strlen(temp_pos)-1] = '\0';
            }
        }
    }
    // PWL_LOG_DEBUG("value: %s\n", value);
    *result = atoi(value);
    fclose(fp);
    return 0;
}

int bootup_status_init() {
    FILE *fp = NULL;

    if (0 == access(BOOTUP_STATUS_RECORD, F_OK)) {
        PWL_LOG_DEBUG("Bootup record file exist");
        fp = fopen(BOOTUP_STATUS_RECORD, "r");

        if (fp == NULL) {
            PWL_LOG_ERR("Open bootup state record file error!");
            return -1;
        }
        fclose(fp);
    } else {
        PWL_LOG_DEBUG("File not exist");
        fp = fopen(BOOTUP_STATUS_RECORD, "w");

        if (fp == NULL) {
            PWL_LOG_ERR("Create bootup state record file error!");
            return -1;
        }
        fprintf(fp, "Bootup_failure_count=0\n");
        fclose(fp);
    }
    return 0;
}

int set_bootup_status_value(char *key, int value) {
    FILE *fp = NULL;
    char line[STATUS_LINE_LENGTH];
    char new_value_line[STATUS_LINE_LENGTH];
    char *new_content = NULL;

    fp = fopen(BOOTUP_STATUS_RECORD, "r+");
    int value_len, file_size, new_file_size;
    value_len = count_int_length(value);
    if (fp == NULL) {
        PWL_LOG_ERR("Open bootup state record file error!");
        return -1;
    }

    // Check size
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);

    new_file_size = file_size + value_len + 1;
    new_content = malloc(new_file_size);
    memset(new_content, 0, new_file_size);

    fseek(fp, 0, SEEK_SET);
    while (fgets(line, STATUS_LINE_LENGTH, fp) != NULL) {
        if (strstr(line, key)) {
            sprintf(new_value_line, "%s=%d\n", key, value);
            strcat(new_content, new_value_line);
        } else {
            strcat(new_content, line);
        }
    }
    fclose(fp);
    fp = fopen(BOOTUP_STATUS_RECORD, "w");

    if (fp == NULL) {
        PWL_LOG_ERR("Create bootup state record file error!\n");
        return -1;
    }
    fwrite(new_content, sizeof(char), strlen(new_content), fp);
    free(new_content);
    fclose(fp);

    return 0;
}

int get_bootup_status_value(char *key, int *result) {
    FILE *fp = NULL;
    char line[STATUS_LINE_LENGTH];
    char *temp_pos = NULL;
    char value[5];

    memset(value, 0, sizeof(value));

    fp = fopen(BOOTUP_STATUS_RECORD, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("Open bootup state record file error!");
        return -1;
    }

    while (fgets(line, STATUS_LINE_LENGTH, fp) != NULL) {
        if (strstr(line, key)) {
            temp_pos = strchr(line, '=');
            ++temp_pos;
            strncpy(value, temp_pos, strlen(temp_pos));
            if (NULL != strstr(value, "\r\n")) {
                PWL_LOG_DEBUG("get result string has carriage return\n");
                value[strlen(temp_pos) - 2] = '\0';
            } else {
                value[strlen(temp_pos) - 1] = '\0';
            }
        }
    }
    // PWL_LOG_DEBUG("value: %s\n", value);
    *result = atoi(value);
    fclose(fp);
    return 0;
}

int read_config_from_file(char *file_name, char*key, int *result) {
    FILE *fp = NULL;
    char line[STATUS_LINE_LENGTH];
    char *temp_pos = NULL;
    char value[5];

    fp = fopen(file_name, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("Open file error!");
        return RET_FAILED;
    }
    while (fgets(line, STATUS_LINE_LENGTH, fp) != NULL) {
        if (strstr(line, key)) {
            temp_pos = strchr(line, '=');
            ++temp_pos;
            strncpy(value, temp_pos, strlen(temp_pos));
            if (NULL != strstr(value, "\r\n")) {
                PWL_LOG_DEBUG("get result string has carriage return\n");
                value[strlen(temp_pos) - 2] = '\0';
            } else {
                value[strlen(temp_pos) - 1] = '\0';
            }
        }
    }
    // PWL_LOG_DEBUG("value: %s\n", value);
    *result = atoi(value);
    fclose(fp);
    return RET_OK;
}

int count_int_length(unsigned x) {
    if (x >= 1000000000) return 10;
    if (x >= 100000000)  return 9;
    if (x >= 10000000)   return 8;
    if (x >= 1000000)    return 7;
    if (x >= 100000)     return 6;
    if (x >= 10000)      return 5;
    if (x >= 1000)       return 4;
    if (x >= 100)        return 3;
    if (x >= 10)         return 2;
    return 1;
}

int remove_folder(char *path) {
    FILE *fp;
    char *ret;
    char command[128] = {0};
    sprintf(command, "rm -rf %s", path);

    PWL_LOG_DEBUG("Remove %s", path);
    fp = popen(command, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("Remove folder error!");
        return RET_FAILED;
    }
    char buffer[MAX_FW_PACKAGE_PATH_LEN] = {0};
    ret = fgets(buffer, sizeof(buffer), fp);
    pclose(fp);
    return RET_OK;
}
