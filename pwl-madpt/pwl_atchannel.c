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
#include <stdio.h>
#include <termios.h>

#include "common.h"
#include "pwl_atchannel.h"

static gchar g_port[20];


gboolean send_at_cmd(const char *port, const char *command, gchar **response) {
    gboolean ret = TRUE;

    size_t command_req_size = strlen(command) + strlen("\r\n") + 1;
    char *command_req = (char *) malloc(command_req_size);
    memset(command_req, 0, command_req_size);
    sprintf(command_req, "%s%s", command, "\r\n");

    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        PWL_LOG_ERR("Error opening serial port\n");
        if (command_req) free(command_req);
        return FALSE;
    }

    // Configure the serial port
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
        PWL_LOG_ERR("Error getting serial port attributes\n");
        ret = FALSE;
        goto exit;
    }

    cfsetospeed(&tty, B115200);         // Set baud rate (e.g., 9600 bps)
    cfsetispeed(&tty, B115200);

    tty.c_cflag |= CLOCAL | CREAD;      // Enable receiver and set local mode
    tty.c_cflag &= ~CSIZE;              // Clear data size bits
    tty.c_cflag |= CS8;                 // Set data bits to 8
    tty.c_cflag &= ~PARENB;             // Disable parity
    tty.c_cflag &= ~CSTOPB;             // Set one stop bit

    tty.c_iflag &= ~INPCK;
    tty.c_iflag |= INPCK;

    tty.c_cc[VTIME] = 0;
    tty.c_cc[VMIN] = 0;

    tcflush(fd, TCIFLUSH);

    // Apply the settings
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        PWL_LOG_ERR("Error applying serial port settings\n");
        ret = FALSE;
        goto exit;
    }

    tcflush(fd, TCIOFLUSH);

    fd_set rset;
    struct timeval time = {PWL_CMD_TIMEOUT_SEC, 0};
    char resp[PWL_MQ_MAX_RESP] = {0};
    int resp_len = 0;

    FD_ZERO(&rset);
    FD_SET(fd, &rset);

    gint bytesWritten = write(fd, command_req, strlen(command_req));
    if (bytesWritten <= 0) {
        PWL_LOG_ERR("%d bytes written error!\n", bytesWritten);
        ret = FALSE;
        goto exit;
    }

    while (select(fd + 1, &rset, NULL, NULL, &time) > 0) {
        char buffer[PWL_MQ_MAX_RESP];
        memset(buffer, 0, sizeof(buffer));

        ssize_t len = read(fd, buffer, sizeof(resp));
        if (len > 0) {
            if (resp_len < PWL_MQ_MAX_RESP) {
                ssize_t copy_len = (((resp_len + len) > PWL_MQ_MAX_RESP) ? (PWL_MQ_MAX_RESP - resp_len) : len);
                memcpy(resp + resp_len, buffer, copy_len);
                resp_len += copy_len;
                if (DEBUG) PWL_LOG_DEBUG("response read(%ld): %s", len, resp);
            } else {
                if (DEBUG) PWL_LOG_DEBUG("long response, skip the rest of the responses");
            }
        } else {
            PWL_LOG_ERR("resp read line resp_len %ld", len);
            break;
        }

        // remove any NULL in response
        for (int i = 0; i < resp_len; i++) {
            //if (DEBUG) PWL_LOG_DEBUG("response[%d]: %X", i, resp[i]);
            if (resp[i] == 0) {
                PWL_LOG_DEBUG("NULL detected in %s response[%d]: %X", command, i, resp[i]);
                memcpy(resp + i, resp + (i + 1), (resp_len - 1 - i));
                resp_len--;
            }
        }

        if (strstr(resp, "OK") || strstr(resp, "ERROR") || strstr(resp, "+CME ERROR")) {
             break;
        }
    }

    if (strlen(resp) != 0) {
        if (DEBUG) PWL_LOG_DEBUG("*Response: %s", resp);
        *response = (gchar *)malloc(resp_len + 1);
        if (*response == NULL) {
            PWL_LOG_ERR("AT Command response malloc failed!!");
            ret = FALSE;
            goto exit;
        }
        memset(*response, 0, resp_len + 1);
        memcpy(*response, resp, resp_len);
    } else {
        PWL_LOG_ERR("AT Command error!! %d", resp_len);
        ret = FALSE;
        goto exit;
    }

exit:

    if (command_req) {
        free(command_req);
        command_req = NULL;
    }

    // Close the serial port
    int close_ret = close(fd);
    PWL_LOG_INFO("Close at %d, result: %d", fd, close_ret);
    if (close_ret != 0) {
        PWL_LOG_ERR("Errno %d %s", errno, strerror(errno));
    }

    return ret;
}

gboolean pwl_atchannel_find_at_port() {
    PWL_LOG_INFO("looking for port..");
    gboolean found = FALSE;
    FILE *fp = NULL;

    pwl_device_type_t type = pwl_get_device_type();
    if (type == PWL_DEVICE_TYPE_USB) {
        fp = popen("find /dev/ -name ttyUSB*", "r");
    } else if (type == PWL_DEVICE_TYPE_PCIE) {
        fp = popen("find /dev/ -name wwan0at*", "r");
    }

    if (fp == NULL) {
        PWL_LOG_ERR("find at port cmd error!!!");
        return found;
    }

    char port[20];
    memset(port, 0, sizeof(port));
    while (fgets(port, sizeof(port), fp) != NULL) {
        port[strcspn(port, "\n")] = 0;

        gchar *response = NULL;
        gboolean result = send_at_cmd(port, "AT", &response);
        if (!result) {
            sleep(3);
            PWL_LOG_INFO("retry 2nd time for %s", port);
            if (response != NULL) {
                free(response);
            }
            send_at_cmd(port, "AT", &response);
        }

        if (response != NULL) {
            if (strstr(response, "OK") || strstr(response, "ERROR")) {
                strncpy(g_port, port, strlen(port));
                found = TRUE;
                free(response);
                break;
            }
            free(response);
        }
    }
    pclose(fp);

    return found;
}

gboolean pwl_atchannel_at_req(const gchar *command, gchar **response) {
    if (DEBUG) PWL_LOG_DEBUG("AT Command: %s", command);

    if (strlen(g_port) == 0) {
        if (!pwl_atchannel_find_at_port()) {
            PWL_LOG_ERR("No AT port found\n");
            return FALSE;
        }
    }

    return send_at_cmd(g_port, command, response);
}

gboolean pwl_atchannel_at_port_wait() {
    for (int i = 0; i < 10; i++) {

        FILE *fp = NULL;
        pwl_device_type_t type = pwl_get_device_type();
        if (type == PWL_DEVICE_TYPE_USB) {
            fp = popen("find /dev/ -name ttyUSB*", "r");
        } else if (type == PWL_DEVICE_TYPE_PCIE) {
            fp = popen("find /dev/ -name wwan0at*", "r");
        }

        if (fp == NULL) {
            PWL_LOG_ERR("find at port cmd error!!!");
            continue;
        }
        char port[20];
        memset(port, 0, sizeof(port));
        if (fgets(port, sizeof(port), fp) != NULL) {
            PWL_LOG_INFO("AT port wait... found");
            pclose(fp);
            return TRUE;
        }
        pclose(fp);
        sleep(5);
    }
    return FALSE;
}
