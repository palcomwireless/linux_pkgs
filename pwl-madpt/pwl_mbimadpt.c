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

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/msg.h>
#include <unistd.h>

#include "common.h"
#include "pwl_mbimadpt.h"

#define IOCTL_WDM_MAX_COMMAND       _IOR('H', 0xA0, gushort)

pthread_cond_t g_mbim_req_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t g_mbim_req_mutex = PTHREAD_MUTEX_INITIALIZER;

static guint32 g_Transaction_ID = 1;
gushort g_wdm_max = 0;
gint g_fd = -1;
static mbim_cmd_response_t *g_cmd_done_response = NULL;


void parse_open_done(gchar *read_buff_ptr, guint32 read_buff_size) {
    mbim_open_done_t *open_done_msg = (mbim_open_done_t *)read_buff_ptr;

    if (read_buff_size != open_done_msg->message_header.message_length) {
        PWL_LOG_ERR("incompatible open_done_msg size. read_buff_size=%d!", read_buff_size);
        return;
    }

    if (open_done_msg->status == MBIM_STATUS_SUCCESS) {
        //PWL_LOG_DEBUG("Open done no error.");
    } else {
        PWL_LOG_ERR("Open failed with error code=%d", open_done_msg->status);
    }
}

void parse_close_done(gchar *read_buff_ptr, guint32 read_buff_size) {

    mbim_close_done_t *close_done_msg = (mbim_close_done_t *)read_buff_ptr;

    if (read_buff_size != close_done_msg->message_header.message_length) {
        PWL_LOG_ERR("incompatible close_done_msg size. read_buff_size=%d!", read_buff_size);
        return;
    }

    if (close_done_msg->status == MBIM_STATUS_SUCCESS) {
        //PWL_LOG_DEBUG("Close done no error.");
    } else {
        PWL_LOG_ERR("Close failed with error code=%d", close_done_msg->status);
    }
}

void parse_command_done(gchar *read_buff_ptr, guint32 read_buff_size) {

    mbim_command_done_t *cmd_done_msg = (mbim_command_done_t *)read_buff_ptr;

    //PWL_LOG_DEBUG("parse_command_done.");

    if (read_buff_size != cmd_done_msg->message_header.message_length) {
        PWL_LOG_ERR("incompatible parse_command_done size. read_buff_size=%d!", read_buff_size);
        return;
    }

    if (cmd_done_msg->status == MBIM_STATUS_SUCCESS) {
        //PWL_LOG_DEBUG("Command done no error.");
    } else {
        PWL_LOG_ERR("Command failed with error code=%d", cmd_done_msg->status);
        pthread_mutex_lock(&g_mbim_req_mutex);
        if (g_cmd_done_response != NULL) {
            PWL_LOG_ERR("g_cmd_done_response is not NULL. free it.");
            free(g_cmd_done_response);
            g_cmd_done_response = NULL;
        }
        pthread_cond_signal(&g_mbim_req_cond);
        pthread_mutex_unlock(&g_mbim_req_mutex);
        return;
    }
}

void parse_func_error(gchar *read_buff_ptr, guint32 read_buff_size) {

    mbim_function_error_msg_t *func_err = (mbim_function_error_msg_t *)read_buff_ptr;

    PWL_LOG_ERR("MBIM function error. transaction_id is %d", func_err->message_header.transaction_id);
    PWL_LOG_ERR("Error code=%d.", func_err->error_status_code);
}

void parse_indicate_status(gchar *read_buff_ptr, guint32 read_buff_size) {

    mbim_indicate_status_msg_t *ind_status_msg = (mbim_indicate_status_msg_t *)read_buff_ptr;

    if (read_buff_size != ind_status_msg->mbim_message_header.message_length) {
        PWL_LOG_ERR("incompatible parse_indicate_status size. read_buff_size=%d!", read_buff_size);
        return ;
    }
}

void pwl_mbimadpt_at_req(gchar *command) {
    if (DEBUG) PWL_LOG_DEBUG("cmd: %s", command);
    guint total_buff = 0;
    mbim_at_cmd_t at_cmd = {0};

    memset(&at_cmd, 0, sizeof(at_cmd));
    at_cmd.at_cmd_len = strlen(command) + 1;
    strncpy(at_cmd.at_cmd, command, strlen(command)); 

    total_buff = sizeof(mbim_command_msg_format_t) + sizeof(at_cmd);
    //PWL_LOG_DEBUG("sizeof allocated %ld", total_buff);
    
    mbim_command_msg_format_t* mbim_ctl_msg = (mbim_command_msg_format_t*) malloc(total_buff);
    if (mbim_ctl_msg == NULL) {
        PWL_LOG_ERR("mbim_build_control_message() allocate fail.");
        return;
    }

    memset(mbim_ctl_msg, 0, total_buff);

    mbim_ctl_msg->message_header.message_type = MBIM_COMMAND_MSG;
    mbim_ctl_msg->message_header.message_length = total_buff;
    mbim_ctl_msg->message_header.transaction_id = g_Transaction_ID++;

    mbim_ctl_msg->fragment_header.total_fragments = 1;
    mbim_ctl_msg->fragment_header.current_fragment = 0;

    memcpy(&mbim_ctl_msg->device_service_id, &uuid_compal, sizeof(mbim_uuid_t));
    mbim_ctl_msg->cid = 1;
    mbim_ctl_msg->command_type = MBIM_MSG_TYPE_QUERY;

    memcpy(mbim_ctl_msg->information_buffer, &at_cmd, sizeof(at_cmd));
    mbim_ctl_msg->information_buffer_length = sizeof(at_cmd);

    int bytesWritten = write(g_fd, mbim_ctl_msg, total_buff);
    free(mbim_ctl_msg);
}

gint response_msg_parsing(gchar *read_buff_ptr, guint32 read_buff_size) {
    mbim_message_header_t *msg_head;
    mbim_msg_type_t msg_type;

    //PWL_LOG_DEBUG("response_msg_parsing()");

    if (read_buff_ptr == NULL || read_buff_size == 0) {
        PWL_LOG_ERR("empty buffer!");
        return 0;
    }

    // check the message header first
    msg_head = (mbim_message_header_t *)read_buff_ptr;
    memcpy(&msg_type, &msg_head->message_type, sizeof(mbim_msg_type_t));

    PWL_LOG_DEBUG("message type = 0x%08x", msg_type);

    switch (msg_type)
    {
        case MBIM_OPEN_DONE:
            parse_open_done(read_buff_ptr, read_buff_size);
            break;
        case MBIM_CLOSE_DONE:
            parse_close_done(read_buff_ptr, read_buff_size);
            break;
        case MBIM_COMMAND_DONE:
            parse_command_done(read_buff_ptr, read_buff_size);
            break;
        case MBIM_FUNCTION_ERROR_MSG:
            parse_func_error(read_buff_ptr, read_buff_size);
            break;
        case MBIM_INDICATE_STATUS_MSG:
            parse_indicate_status(read_buff_ptr, read_buff_size);
            break;
        case MBIM_MSG_TYPE_INVALID:
            break;
        default:
            PWL_LOG_ERR("Unknown message type: 0x%x", msg_type);
            break;
    }
    return 0;
}

gboolean pwl_mbimadpt_resp_parsing(const gchar *rsp, gchar *buff_ptr, guint32 buff_size) {
    if (rsp == NULL) {
        PWL_LOG_ERR("response empty");
        memset(buff_ptr, '\0', buff_size);
        return FALSE;
    }

    gchar* head = strstr(rsp, "\n");
    gchar* start = NULL;
    if (head != NULL) {
        start = head + 1;
        // check if start with more '\n'
        while (strncmp(start, "\n", strlen("\n")) == 0) {
            start = start + 1;
        }
    }

    // look for OK or ERROR in response
    gchar* end = strstr(rsp, "\r\n\r\nOK");
    if (end == NULL)
    {
        end = strstr(rsp, "OK");
        if (strstr(rsp, "ERROR") != NULL) {
            return FALSE;
        }
    }

    if (head == NULL || start == NULL || end == NULL) {
        PWL_LOG_ERR("Error parsing response");
        return FALSE;
    }

    gint size = end - start;
    if (buff_size > strlen(start)) {
        memset(buff_ptr, '\0', buff_size);
        if (size == 0) { // response of just 'OK'
            strncpy(buff_ptr, start, strlen(start));
        } else {
            strncpy(buff_ptr, start, size);
        }
        return TRUE;
    } else {
        PWL_LOG_ERR("Response buffer size not large enough");
        return FALSE;
    }
}

gboolean pwl_mbimadpt_find_port(gchar *port_buff_ptr, guint32 port_buff_size) {
    return pwl_find_mbim_port(port_buff_ptr, port_buff_size);
}

gint pwl_mbimadpt_mbim_open() {

    gchar port[20];
    memset(port, 0, sizeof(port));
    if (!pwl_mbimadpt_find_port(port, sizeof(port))) {
        PWL_LOG_ERR("find mbim port fail!");
        return -1;
    }

    gint fd = open(port, O_RDWR);
    if (DEBUG) PWL_LOG_DEBUG("open port: %s (fd %d)", port, fd);

    if (fd < 0) {
        PWL_LOG_ERR("open %s fail!", port);
        return -1;
    }

    if (ioctl(fd, IOCTL_WDM_MAX_COMMAND, &g_wdm_max))
        PWL_LOG_ERR("ioctl fail");

    PWL_LOG_INFO("Open mbim %d", fd);

    return fd;
}

void pwl_mbimadpt_mbim_close(gint *fd) {
    int ret = close(*fd);
    PWL_LOG_INFO("Close mbim %d, result: %d", *fd, ret);
    if (ret != 0) {
        PWL_LOG_ERR("Errno %d %s", errno, strerror(errno));
    }
    *fd = -1;
}

gboolean pwl_mbimadpt_init(gint mbim_fd) {
    g_fd = mbim_fd;

    pthread_cond_init(&g_mbim_req_cond, NULL);
    pthread_mutex_init(&g_mbim_req_mutex, NULL);

    mbim_open_message_t* open_msg = (mbim_open_message_t*) malloc(sizeof(mbim_open_message_t));
    if (open_msg == NULL) {
        PWL_LOG_ERR("open message allocation failed");
        return FALSE;
    }

    open_msg->message_header.message_type = MBIM_OPEN_MSG;
    open_msg->message_header.message_length = sizeof(mbim_open_message_t);
    open_msg->message_header.transaction_id = g_Transaction_ID++;
    open_msg->max_ctrl_transfer = g_wdm_max;

    int bytesWritten = write(g_fd, open_msg, sizeof(mbim_open_message_t));
    free(open_msg);

    PWL_LOG_INFO("waiting for mbim port init");
    if (!cond_wait(&g_mbim_req_mutex, &g_mbim_req_cond, PWL_OPEN_MBIM_TIMEOUT_SEC)) {
        PWL_LOG_ERR("timed out or error for mbim port init wait");
        return FALSE;
    }

    PWL_LOG_INFO("mbim port init completed");
    return TRUE;
}

void pwl_mbimadpt_mbim_msg_signal() {
    pthread_cond_signal(&g_mbim_req_cond);
}

gboolean pwl_mbimadpt_deinit_async() {
    pwl_mbimadpt_deinit();

    PWL_LOG_INFO("waiting for mbim port deinit");
    if (!cond_wait(&g_mbim_req_mutex, &g_mbim_req_cond, PWL_OPEN_MBIM_TIMEOUT_SEC)) {
        PWL_LOG_ERR("timed out or error for mbim port deinit wait");
        return FALSE;
    }

    PWL_LOG_INFO("mbim port deinit completed");
    return TRUE;
}

void pwl_mbimadpt_deinit() {
    mbim_close_message_t* close_msg = (mbim_close_message_t*) malloc(sizeof(mbim_close_message_t));
    if (close_msg == NULL) {
        PWL_LOG_ERR("close message allocation failed");
        return;
    }

    close_msg->message_header.message_type = MBIM_CLOSE_MSG;
    close_msg->message_header.message_length = sizeof(mbim_close_message_t);
    close_msg->message_header.transaction_id = g_Transaction_ID++;

    int bytesWritten = write(g_fd, close_msg, sizeof(mbim_close_message_t));
    free(close_msg);
}
