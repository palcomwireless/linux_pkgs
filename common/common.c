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

gboolean pwl_module_usb_id_exist(gchar *usbid) {

    gchar command[] = "lsusb | grep ";

    gchar cmd[strlen(command) + strlen(usbid) + 1];
    memset(cmd, 0, sizeof(cmd));
    sprintf(cmd, "%s%s", command, usbid);

    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("usb id check cmd error!!!");
        return FALSE;
    }

    char response[200];
    memset(response, 0, sizeof(response));
    char *ret = fgets(response, sizeof(response), fp);

    pclose(fp);

    if (strlen(response) > 0)
        return TRUE;

    return FALSE;
}

gboolean cond_wait(pthread_mutex_t *mutex, pthread_cond_t *cond, gint wait_time) {
    pthread_mutex_unlock(mutex);
    pthread_mutex_lock(mutex);
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += wait_time;

    int result = pthread_cond_timedwait(cond, mutex, &timeout);
    if (result == ETIMEDOUT || result != 0) {
        PWL_LOG_ERR("timed out or error!!!");
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

    FILE *fp = popen("find /dev/ -name cdc-wdm*", "r");

    if (fp == NULL) {
        PWL_LOG_ERR("find port cmd error!!!");
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
    FILE *fp;

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
        fclose(fp);
    }
    return 0;
}

int set_fw_update_status_value(char *key, int value) {
    FILE *fp;
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
    FILE *fp;
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
