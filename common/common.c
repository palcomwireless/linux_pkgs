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
    int msg_prio;
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
    fgets(response, sizeof(response), fp);
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
                      message->response);
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
    fgets(buffer, sizeof(buffer), fp);
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

#define PWL_CMD_SET     "mbimcli -d %s -p --compal-set-at-command=\"%s\""

gboolean pwl_set_command(const gchar *command, gchar **response) {
    if (DEBUG) PWL_LOG_DEBUG("Set Command %s", command);

    gchar port[20];
    memset(port, 0, sizeof(port));
    if (!pwl_find_mbim_port(port, sizeof(port))) {
        PWL_LOG_ERR("find mbim port fail!");
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
        if (DEBUG) PWL_LOG_DEBUG("query resp: %s", line);
        strcat(buff, line);
    }
    pclose(fp);

    if (DEBUG) PWL_LOG_DEBUG("Response(%ld): %s", strlen(buff), buff);

    if (strlen(buff) > 0) {
        *response = (gchar *)malloc(strlen(buff));
        if (*response == NULL) {
            PWL_LOG_ERR("Command response malloc failed!!");
            return FALSE;
        }
        memset(*response, 0, strlen(buff));
        memcpy(*response, buff, strlen(buff) - 1);
    } else {
        return FALSE;
    }

    return TRUE;
}