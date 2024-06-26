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
#include <sys/ioctl.h>
#include <sys/msg.h>
#include <unistd.h>
#include <stdio.h>

#include "common.h"
#include "dbus_common.h"
#include "log.h"
#include "pwl_atchannel.h"
#include "pwl_madpt.h"
#include "pwl_mbimadpt.h"
#include "pwl_mbimdeviceadpt.h"


#define ATCMD_INDEX_MAP(cid) \
    ((cid > PLW_CID_MAX_PREF && cid < PLW_CID_MAX_MADPT) ? (cid - PLW_CID_MAX_PREF - 1) : 1)

gchar* at_cmd_map[] = {
    "at+cimi",
    "ate",
    "ati",
    "at*bfwver",
    "",
    "",
    "at*bboothold",
    "at*bpriid?",
    "at*bimpref?",
    "at*bimpref=",
    "at*mdpver=",
    "at*mtunecodedel=1",
    "at+crsm=176,28589,0,0,4",
    "at*ucomp=1,1,1018",
    "at*breset",
    "at*mgetoempriinfo=1",
    "at*bjpfccautoreboot?",
    "at*bjpfccautoreboot=1",
    "at*boemprireset?"
};

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;

gboolean g_restart_modem = FALSE;
gint g_exit_code = EXIT_SUCCESS;
pwl_at_intf_t g_at_intf = PWL_AT_INTF_NONE;
GThread *g_mbim_recv_thread = NULL;
gint g_mbim_fd = -1;
uint32_t mbim_err_cnt = 0;
char g_response[PWL_MQ_MAX_RESP];
int g_oem_pri_state = -1;

static GMainLoop *gp_loop = NULL;
static pwlCore *gp_proxy = NULL;
static gulong g_ret_signal_handler[RET_SIGNAL_HANDLE_SIZE];
//static signal_callback_t g_signal_callback;
//static method_callback_t g_method_callback;

#if defined(AT_OVER_MBIM_API)
static void mbim_device_ready_cb();
static void mbim_at_resp_cb(const gchar* response);
#endif

pwl_cid_status_t at_cmd_request(gchar *command) {
#if defined(AT_OVER_MBIM_API)
    pwl_mbimdeviceadpt_at_req(command, mbim_at_resp_cb);
    return PWL_CID_STATUS_OK;

#else
    if (g_at_intf == PWL_AT_OVER_MBIM_CONTROL_MSG) {
        pwl_mbimadpt_at_req(command);
        return PWL_CID_STATUS_OK;
    } else if (g_at_intf == PWL_AT_CHANNEL || g_at_intf == PWL_AT_OVER_MBIM_CLI) {
        gchar *response = NULL;
        gboolean res = FALSE;
        if (g_at_intf == PWL_AT_OVER_MBIM_CLI) {
            res = pwl_set_command(command, &response);
        } else {
            res = pwl_atchannel_at_req(command, &response);
        }
        if (res) {
            if (!pwl_mbimadpt_resp_parsing(response, g_response, PWL_MQ_MAX_RESP)) {
                return PWL_CID_STATUS_ERROR;
            }
        } else {
            return PWL_CID_STATUS_ERROR;
        }
        if (response) {
            free(response);
            return PWL_CID_STATUS_OK;
        }
        return PWL_CID_STATUS_ERROR;
    }
    return PWL_CID_STATUS_ERROR;
#endif
}

#if defined(AT_OVER_MBIM_API)
static void mbim_device_ready_cb() {
    //Send signal to pwl_pref to get fw version
    pwl_core_call_madpt_ready_method (gp_proxy, NULL, NULL, NULL);
}

static void mbim_at_resp_cb(const gchar* response) {
    if (strcmp(response, "ERROR") == 0) {
        mbim_err_cnt = PWL_MBIM_ERR_MAX;
    } else {
        pwl_mbimadpt_resp_parsing(response, g_response, PWL_MQ_MAX_RESP);
        pthread_cond_signal(&g_cond);
    }
}
#endif

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
gboolean register_client_signal_handler(pwlCore *p_proxy) {
    PWL_LOG_DEBUG("register_client_signal_handler call.");
    g_ret_signal_handler[0] = g_signal_connect(p_proxy, "notify::g-name-owner", G_CALLBACK(cb_owner_name_changed_notify), NULL);
    return TRUE;
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
    return TRUE;
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

void send_message_queue(uint32_t cid) {
    mqd_t mq;
    mq = mq_open(CID_DESTINATION(cid), O_WRONLY);

    // message to be sent
    msg_buffer_t message;
    message.pwl_cid = cid;
    message.status = PWL_CID_STATUS_NONE;
    message.sender_id = PWL_MQ_ID_MADPT;

    // msgsnd to send message
    mq_send(mq, (gchar *)&message, sizeof(message), 0);
}

static gpointer mbim_recv_thread_func(gpointer data) {
    gchar read_buff[MBIM_MAX_RESP_SIZE];
    struct pollfd recv_control;

    memset(&recv_control, 0, sizeof(recv_control));
    recv_control.fd = g_mbim_fd;
    recv_control.events = POLLIN;

    while (g_mbim_fd > 0) {
        if (poll(&recv_control, 1, 100) != -1) {
            if (recv_control.revents & POLLIN) {
                guint32 read_size = 0;
                read_size = read(g_mbim_fd, read_buff, MBIM_MAX_RESP_SIZE);

                if (read_size > 0) {
                    mbim_message_header_t *msg_head;
                    mbim_msg_type_t msg_type;

                    if (read_buff == NULL || read_size == 0) {
                        PWL_LOG_ERR("empty buffer!");
                        continue;
                    }

                    // check the message header first
                    msg_head = (mbim_message_header_t *)read_buff;
                    memcpy(&msg_type, &msg_head->message_type, sizeof(mbim_msg_type_t));

                    if (DEBUG) response_msg_parsing(read_buff, read_size);

                    if (msg_type == MBIM_COMMAND_DONE) {

                        mbim_command_done_t *cmd_done_msg = (mbim_command_done_t *)read_buff;
                        mbim_at_cmd_resp_t *resp = (mbim_at_cmd_resp_t *)cmd_done_msg->information_buffer;

                        if (strlen(resp->at_cmd_rsp) <= 0) {
                            continue;
                        }

                        if (strlen(resp->at_cmd_rsp) >= PWL_MQ_MAX_RESP) {
                            PWL_LOG_ERR("MQ response buffer size not enough!!");
                        }

                        if (DEBUG) {
                            PWL_LOG_DEBUG("Response(%ld): %s", strlen(resp->at_cmd_rsp), resp->at_cmd_rsp);
                        }

                        if (cmd_done_msg->status == MBIM_STATUS_SUCCESS) {
                            pwl_mbimadpt_resp_parsing(resp->at_cmd_rsp, g_response, PWL_MQ_MAX_RESP);
                            pthread_cond_signal(&g_cond);
                        }
                    } else if (msg_type == MBIM_CLOSE_DONE) {
                        pwl_mbimadpt_mbim_msg_signal();

                        PWL_LOG_INFO("mbim recv exit");
                        g_mbim_recv_thread = NULL;
                        pthread_exit(NULL);
                    } else if (msg_type == MBIM_OPEN_DONE) {
                        PWL_LOG_DEBUG("!!! === Open Done! === ");
                        pwl_mbimadpt_mbim_msg_signal();
                    }
                }
            }
        }
    }
    return ((void*)0);
}

gboolean mbim_init() {
    mbim_err_cnt = 0;

    // close any previous opened port
    if (g_mbim_fd >= 0) {
        PWL_LOG_ERR("close any previous open mbim %d", g_mbim_fd);
        pwl_mbimadpt_deinit_async();
        pwl_mbimadpt_mbim_close(&g_mbim_fd);
    }

    g_mbim_fd = pwl_mbimadpt_mbim_open();
    if (g_mbim_fd < 0) {
        PWL_LOG_ERR("mbim port open fail");
        return FALSE;
    } else {
        PWL_LOG_INFO("mbim recv start");
        g_mbim_recv_thread = g_thread_new("mbim_recv_thread", mbim_recv_thread_func, NULL);
        if (!pwl_mbimadpt_init(g_mbim_fd))
            return FALSE;
    }

    return TRUE;
}

gboolean mbim_open_status_check(msg_buffer_t* message) {
    static uint32_t mbim_open_wait_cnt = 0;

    // if next cid is to open mbim, then we continue to process cid
    if (message->pwl_cid == PWL_CID_OPEN_MBIM_RECV)
        return TRUE;

    if (g_mbim_fd < 0 || mbim_err_cnt >= PWL_MBIM_ERR_MAX) {

        PWL_LOG_ERR("%d Reply mbim busy to sender %s, cid is %d", mbim_open_wait_cnt,
                    PWL_MQ_PATH(message->sender_id), message->pwl_cid);
        send_message_reply(message->pwl_cid, PWL_MQ_ID_MADPT, message->sender_id, PWL_CID_STATUS_BUSY, "Timeout");

        if (mbim_open_wait_cnt < PWL_MBIM_OPEN_WAIT_MAX && mbim_err_cnt < PWL_MBIM_ERR_MAX) {
            // waiting for someone to open mbim, perhaps fcc unlock
            mbim_open_wait_cnt++;
            return FALSE;
        } else {
            // wait too many times, open mbim
            PWL_LOG_ERR("Waited too many times, try to open mbim again");
            mbim_init();
            return FALSE;
        }
    } else {
        mbim_open_wait_cnt = 0;
    }

    return TRUE;
}

static gpointer msg_queue_thread_func(gpointer data) {
    mqd_t mq;
    struct mq_attr attr;
    msg_buffer_t message;
    pwl_cid_status_t status = PWL_CID_STATUS_OK;
    char *cust_set_cmd;
    char device_package_ver[DEVICE_PACKAGE_VERSION_LENGTH];
    int cmd_len = 0;
    /* initialize the queue attributes */
    attr.mq_flags = 0;
    attr.mq_maxmsg = PWL_MQ_MAX_MSG;
    attr.mq_msgsize = sizeof(message);
    attr.mq_curmsgs = 0;

    /* create the message queue */
    mq = mq_open(PWL_MQ_PATH_MADPT, O_CREAT | O_RDONLY, 0644, &attr);

    while (1) {
        ssize_t bytes_read;

        /* receive the message */
        bytes_read = mq_receive(mq, (gchar *)&message, sizeof(message), NULL);

        if (g_at_intf == PWL_AT_OVER_MBIM_CONTROL_MSG) {
            if (!mbim_open_status_check(&message))
                continue;
        }

        print_message_info(&message);

        gboolean timedwait = TRUE;
        status = PWL_CID_STATUS_OK;
        memset(g_response, 0, PWL_MQ_MAX_RESP);

        if (g_at_intf == PWL_AT_OVER_MBIM_CONTROL_MSG) {
            if (message.pwl_cid == PWL_CID_SUSPEND_MBIM_RECV) {
                pwl_mbimadpt_deinit_async();
                timedwait = FALSE; // let mbimadpt to handle close wait
            } else if (message.pwl_cid == PWL_CID_OPEN_MBIM_RECV) {
                // Sleep for 3 seconds so port can be successfully
                // find at next open request
                sleep(3);

                if (g_mbim_fd < 0) {
                    mbim_init();
                } else {
                    PWL_LOG_INFO("mbim open already, skip open");
                }
                timedwait = FALSE; // let mbimadpt to handle open wait
            }
        }

        if (message.pwl_cid == PWL_CID_MADPT_RESTART) {
            restart();
            timedwait = FALSE;
        } else if (message.pwl_cid == PWL_CID_SET_PREF_CARRIER) { 
            if (DEBUG) PWL_LOG_DEBUG("PWL_CID_SET_PREF_CARRIER");
            cmd_len = strlen(at_cmd_map[ATCMD_INDEX_MAP(message.pwl_cid)]) + strlen(message.content) + 1;
            cust_set_cmd = (char *) malloc(cmd_len);
            memset(cust_set_cmd, '0', cmd_len);

            strcpy(cust_set_cmd, at_cmd_map[ATCMD_INDEX_MAP(message.pwl_cid)]);
            strcat(cust_set_cmd, message.content);
            if (DEBUG) {
                PWL_LOG_DEBUG("set pref cmd: %s", cust_set_cmd);
            } else {
                PWL_LOG_INFO("set pref to: %s", message.content);
            }
            status = at_cmd_request(cust_set_cmd);
            free(cust_set_cmd);
        } else if (message.pwl_cid == PWL_CID_SET_OEM_PRI_VERSION) {
            if (DEBUG) PWL_LOG_DEBUG("PWL_CID_SET_OEM_PRI_VERSION");
            if (strstr(message.content, "DPV")) {
                strncpy(device_package_ver, message.content, DEVICE_PACKAGE_VERSION_LENGTH - 1);
                PWL_LOG_INFO("OEM_PRI Device package: %s", device_package_ver);
            } else {
                strcpy(device_package_ver, "DPV00.00.00.01");
            }

            cmd_len = strlen(at_cmd_map[ATCMD_INDEX_MAP(message.pwl_cid)]) + 
                    strlen(device_package_ver) + 3;
            cust_set_cmd = (char *) malloc(cmd_len);
            memset(cust_set_cmd, '0', cmd_len);

            strcpy(cust_set_cmd, at_cmd_map[ATCMD_INDEX_MAP(message.pwl_cid)]);
            strcat(cust_set_cmd, "\"");
            strcat(cust_set_cmd, device_package_ver);
            strcat(cust_set_cmd, "\"");

            status = at_cmd_request(cust_set_cmd);
            free(cust_set_cmd);
        } else {
            status = at_cmd_request(at_cmd_map[ATCMD_INDEX_MAP(message.pwl_cid)]);
        }

        if (timedwait &&
           (g_at_intf == PWL_AT_OVER_MBIM_CONTROL_MSG || g_at_intf == PWL_AT_OVER_MBIM_API)) {
            pthread_mutex_lock(&g_mutex);
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += PWL_CMD_TIMEOUT_SEC;

            int result = pthread_cond_timedwait(&g_cond, &g_mutex, &timeout);
            if (result == ETIMEDOUT || result != 0) {
                mbim_err_cnt++;
                PWL_LOG_ERR("timed out or error for cid %s", cid_name[message.pwl_cid]);
                status = PWL_CID_STATUS_TIMEOUT;
            } else {
                mbim_err_cnt = 0;
            }
            pthread_mutex_unlock(&g_mutex);
        }

#if defined(AT_OVER_MBIM_API)
        if (g_at_intf == PWL_AT_OVER_MBIM_API) {
            if (mbim_err_cnt >= PWL_MBIM_ERR_MAX) {
                pwl_mbimdeviceadpt_deinit();
                pwl_mbimdeviceadpt_init(mbim_device_ready_cb);
                mbim_err_cnt = 0;
            }
        }
#endif

        PWL_LOG_INFO("fd (%d), total error (%d)", g_mbim_fd, mbim_err_cnt);

        switch (message.pwl_cid)
        {   
            case PWL_CID_GET_ATE:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, "OK");
                break;
            case PWL_CID_GET_ATI:
                // PWL_LOG_DEBUG("g_response: %s", g_response);
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_GET_FW_VER:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_SUSPEND_MBIM_RECV:
                pwl_mbimadpt_mbim_close(&g_mbim_fd);

                // Sleep for 3 seconds so port can be successfully
                // find at next open request
                sleep(3);

                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, "OK");
                break;
            case PWL_CID_OPEN_MBIM_RECV:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, "Mbim open done!");
                break;
            case PWL_CID_SWITCH_TO_FASTBOOT:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, "Switch to fastboot cmd done");
                break;
            case PWL_CID_CHECK_OEM_PRI_VERSION:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_GET_PREF_CARRIER:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_DEL_TUNE_CODE:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_SET_PREF_CARRIER:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_SET_OEM_PRI_VERSION:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_GET_CRSM:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_GET_CIMI:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            default:
                PWL_LOG_ERR("Unknown pwl cid: %d", message.pwl_cid);
                break;
        }
    }

    return NULL;
}

void enable_jp_fcc_auto_reboot() {
    // enable this flag so modem will do switch when sim changed to corresponding carrier
    pwl_get_enable_state_t state = PWL_CID_GET_ENABLE_STATE_ERROR;

    for (gint i = 0; i < PWL_OEM_PRI_RESET_RETRY; i++) {
        at_cmd_request(at_cmd_map[ATCMD_INDEX_MAP(PWL_CID_GET_JP_FCC_AUTO_REBOOT)]);

        if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
            PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_GET_JP_FCC_AUTO_REBOOT]);
        } else {
            if (strlen(g_response) != 0) {
                state = atoi(g_response);
            }
            if (state == PWL_CID_GET_ENABLE_STATE_ERROR) {
                PWL_LOG_INFO("JP fcc auto reboot error state");
            } else if (state == PWL_CID_GET_ENABLE_STATE_DISABLED) {
                PWL_LOG_INFO("JP fcc auto reboot state is disabled");

                // enable JP FCC Auto Reboot
                at_cmd_request(at_cmd_map[ATCMD_INDEX_MAP(PWL_CID_ENABLE_JP_FCC_AUTO_REBOOT)]);

                if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
                    PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_ENABLE_JP_FCC_AUTO_REBOOT]);
                }

            } else if (state == PWL_CID_GET_ENABLE_STATE_ENABLED) {
                PWL_LOG_INFO("JP fcc auto reboot state enabled");
                return;
            }
            sleep(1);
        }
    }
}

void wait_for_modem_oem_pri_reset() {
    gint oem_reset_state = OEM_PRI_RESET_NOT_READY;

    for (gint i = 0; i < PWL_OEM_PRI_RESET_RETRY; i++) {
        sleep(1);
        at_cmd_request(at_cmd_map[ATCMD_INDEX_MAP(PWL_CID_GET_OEM_PRI_RESET)]);

        if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
            PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_GET_OEM_PRI_RESET]);
        } else {
            if (strlen(g_response) != 0) {
                oem_reset_state = atoi(g_response);
            }
            PWL_LOG_INFO("oem pri reset state %d", oem_reset_state);

            if (oem_reset_state == OEM_PRI_RESET_NOT_READY) {
                PWL_LOG_INFO("oem pri reset not ready, keep waiting...");
            } else if (oem_reset_state == OEM_PRI_RESET_UPDATE_SUCCESS ||
                       oem_reset_state == OEM_PRI_RESET_UPDATE_FAILED ||
                       oem_reset_state == OEM_PRI_RESET_NO_NEED_UPDATE) {
                return;
            }
        }
    }
}


gboolean check_modem_intf() {
    gboolean is_mbim_ready = FALSE;

    if (MADPT_FORCE_MBIMCLI) {
        g_at_intf = PWL_AT_OVER_MBIM_CLI;
        PWL_LOG_INFO("Force mbim cli");
    } else if (pwl_atchannel_find_at_port()) {
        g_at_intf = PWL_AT_CHANNEL;
        PWL_LOG_INFO("AT Channel");
    } else if (pwl_set_command_available()) {
        PWL_LOG_INFO("Enabling at port by command");

        gchar *response = NULL;
        if (pwl_set_command(at_cmd_map[ATCMD_INDEX_MAP(PWL_CID_AT_UCOMP)], &response)) {
            if (response != NULL && strstr(response, "OK") != NULL) {
                free(response);
                response = NULL;

                if (pwl_set_command(at_cmd_map[ATCMD_INDEX_MAP(PWL_CID_RESET)], &response)) {
                    if (response != NULL && strstr(response, "OK") != NULL) {
                        free(response);
                        response = NULL;

                        for (int i = 0; i < 2; i++) {
                            PWL_LOG_INFO("waiting for at port...");
                            sleep(40);
                            if (pwl_atchannel_find_at_port()) {
                                g_at_intf = PWL_AT_CHANNEL;
                                break;
                            }
                        }
                    }
                }
            }
        } else {
            g_at_intf = PWL_AT_INTF_NONE;
        }
    }
    PWL_LOG_INFO("g_at_intf: %d", g_at_intf);

    if (g_at_intf == PWL_AT_INTF_NONE) {
        g_at_intf = PWL_AT_OVER_MBIM_CONTROL_MSG;
        PWL_LOG_INFO("at mbim control msg, enable at port starts");

        // Check mbim init (timout 3 mins 6s*30)
        for (int i = 0; i < 30; i++) {
            if (!mbim_init()) {
                sleep(6);
                continue;
            } else {
                is_mbim_ready = TRUE;
                break;
            }
        }

        if (!is_mbim_ready) {
            PWL_LOG_ERR("Mbim init failed, do GPIO reset.");
            gdbus_init();
            while(!dbus_service_is_ready());
            PWL_LOG_DEBUG("DBus Service is ready");
            pwl_core_call_gpio_reset_method_sync (gp_proxy, NULL, NULL);
            // hw reset function will send PWL_CID_MADPT_RESTART msg but can't receive at this point
            // so restart it self
            g_exit_code = EXIT_FAILURE;
            return TRUE;
        } else {
            // incase module become alive again with AT port already available
            PWL_LOG_INFO("re-check at port existence");
            if (pwl_atchannel_find_at_port()) {
                pwl_mbimadpt_deinit_async();
                pwl_mbimadpt_mbim_close(&g_mbim_fd);
                g_at_intf = PWL_AT_CHANNEL;
                PWL_LOG_INFO("AT Channel available");
                return FALSE;
            }
        }

        sleep(5); // immediatly use of mbim will cause error, wait a bit

        // Stop Modem Manager since we will interfere with MBIM port
        gint sys_res = system("systemctl stop ModemManager.service");
        PWL_LOG_INFO("Stop Modem Manager res %d", sys_res);

        enable_jp_fcc_auto_reboot();

        // Check oem pri info:
        // START(0)/NORESET(3) > do nothing
        // INIT(1) > wait
        // REST(2) > reset module
        gint retry = 0;
        while (g_oem_pri_state != OEM_PRI_UPDATE_NORESET)
        {
            if (retry > 10) break; // retry wait for OEM_PRI_UPDATE_RESET for 50s then exit

            PWL_LOG_DEBUG("===== Get OEM pri info =====, def: %d", g_oem_pri_state);
            at_cmd_request(at_cmd_map[ATCMD_INDEX_MAP(PWL_CID_GET_OEM_PRI_INFO)]);
            sleep(5);
            retry++;
            g_oem_pri_state = atoi(g_response);
            PWL_LOG_DEBUG("===== OEM info int: %d", g_oem_pri_state);
            if (g_oem_pri_state == OEM_PRI_UPDATE_START || g_oem_pri_state == OEM_PPI_UPDATE_INIT)
            {
                continue;
            }
            else if (g_oem_pri_state == OEM_PRI_UPDATE_RESET)
            {
                PWL_LOG_DEBUG("===== Oem pri need reset =====");
                wait_for_modem_oem_pri_reset();
                break;
            }
            else
            {
                continue;
            }
        }
        PWL_LOG_DEBUG("===== OEM info check END =====");


        for (int i = 0; i < 5; i++) {
            at_cmd_request(at_cmd_map[ATCMD_INDEX_MAP(PWL_CID_AT_UCOMP)]);
            if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
                PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_AT_UCOMP]);
                continue;
            } else {
                at_cmd_request(at_cmd_map[ATCMD_INDEX_MAP(PWL_CID_RESET)]);
                if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
                    PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_RESET]);
                } else {
                    break;
                }
            }
        }

        if (pwl_atchannel_at_port_wait()) {
            g_at_intf = PWL_AT_CHANNEL;
            PWL_LOG_INFO("enable at port completed, success");
        } else {
            PWL_LOG_INFO("enable at port completed, failed");
        }

        g_restart_modem = TRUE;

        g_exit_code = EXIT_FAILURE;

        return TRUE;
    }
    return FALSE;
}

void clean_up() {
#if defined(AT_OVER_MBIM_API)
    pwl_mbimdeviceadpt_deinit();
#endif

   if (g_restart_modem) {
        gint sys_res = system("systemctl restart ModemManager.service");
        PWL_LOG_INFO("Restart Modem Manager res %d", sys_res);
   }
}

void restart() {
    g_exit_code = EXIT_FAILURE;
    if (0 != gp_loop) {
        // call exit() API instead quit loop
        // since sometimes restart call before main loop started to run
        //PWL_LOG_INFO("looper running? %d", g_main_loop_is_running(gp_loop));
        //g_main_loop_quit(gp_loop);
        //g_main_loop_unref(gp_loop);
        clean_up();
        exit(1);
    }
    PWL_LOG_INFO("restarting...");
}

gint main() {
    PWL_LOG_INFO("start");

    if (check_modem_intf()) {
        goto exit;
    }

    pwl_discard_old_messages(PWL_MQ_PATH_MADPT);

#if defined(AT_OVER_MBIM_API)
    while (!pwl_mbimdeviceadpt_init(mbim_device_ready_cb)) {
        sleep(5);
    }
#endif

    GThread *msg_queue_thread = g_thread_new("msg_queue_thread", msg_queue_thread_func, NULL);

    gdbus_init();

    while(!dbus_service_is_ready());
    PWL_LOG_DEBUG("DBus Service is ready");


#if !defined(AT_OVER_MBIM_API) // mbim api case need to wait for device ready first
    //Send signal to pwl_pref to get fw version
    pwl_core_call_madpt_ready_method_sync (gp_proxy, NULL, NULL);
#endif

    send_message_reply(PWL_CID_MADPT_RESTART, PWL_MQ_ID_MADPT, PWL_MQ_ID_FWUPDATE, PWL_CID_STATUS_OK, "Madpt started");

    if (gp_loop != NULL)
        g_main_loop_run(gp_loop);

exit:

    clean_up();

    return g_exit_code;
}
