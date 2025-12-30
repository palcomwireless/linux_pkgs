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
#include <dlfcn.h>

#include "common.h"
#include "dbus_common.h"
#include "log.h"
#include "pwl_atchannel.h"
#include "pwl_madpt.h"
#include "pwl_mbimdeviceadpt.h"


#define ATCMD_INDEX_MAP(cid) \
    ((cid > PLW_CID_MAX_PREF && cid < PLW_CID_MAX_MADPT) ? (cid - PLW_CID_MAX_PREF - 1) : 1)

gchar* at_cmd_map[] = {
    "at+cimi",
    "ate",
    "ati",
    "at*bfwver",
    "at*bsku?",
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
    "at*boemprireset?",
    "at+productinfo=1",
    "at*capversion?",
    "at*copid?",
    "at*coemid?",
    "at*cdpvid?",
    "at+esbp?",
    "at*mresetoempri=1",
    "at+esimenable?",
    "at*bchktestprof?",
    "at*bdeltestprof=1",
    "at*msnrw=",
    "at*mimei="
};

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;

gint g_exit_code = EXIT_SUCCESS;
pwl_at_intf_t g_at_intf = PWL_AT_OVER_MBIM_API;
GThread *g_mbim_recv_thread = NULL;
uint32_t mbim_err_cnt = 0;
char g_response[PWL_MQ_MAX_RESP];
int g_oem_pri_state = -1;

static pwl_device_type_t g_device_type = PWL_DEVICE_TYPE_UNKNOWN;
static GMainLoop *gp_loop = NULL;
static pwlCore *gp_proxy = NULL;
static gulong g_ret_signal_handler[RET_SIGNAL_HANDLE_SIZE];
static signal_callback_t g_signal_callback;
//static method_callback_t g_method_callback;

static void mbim_device_ready_cb(gboolean opened);
static void mbim_at_resp_cb(const gchar* response);

gboolean at_resp_parsing(const gchar *rsp, gchar *buff_ptr, guint32 buff_size) {
    if (rsp == NULL) {
        PWL_LOG_ERR("response empty");
        memset(buff_ptr, '\0', buff_size);
        return FALSE;
    }

    gchar* start = (gchar*) rsp;
    if (strncmp(start, "at+", strlen("at+")) == 0 || strncmp(start, "at*", strlen("at*")) == 0) {
        PWL_LOG_DEBUG("Ignore the AT command at the beginning of the response.");
        start = strstr(start, "\n");
    }

    gchar pcie_buffer[PWL_MQ_MAX_RESP] = {0};
    if (start != NULL) {
        // check if start with '\n'
        while (strncmp(start, "\n", strlen("\n")) == 0) {
            start = start + 1;
        }
    }

    // look for OK or ERROR in response
    gchar* end = strstr(rsp, "\r\n\r\nOK");
    if (end == NULL)
    {
        end = strstr(rsp, "OK");
        if (end == NULL && (strlen(rsp) == PWL_MQ_MAX_RESP)) {
            end = start + PWL_MQ_MAX_RESP;
        }
        if (strstr(rsp, "ERROR") != NULL) {
            return FALSE;
        }
    }

    if (start == NULL || end == NULL) {
        if (start == NULL)
            PWL_LOG_ERR("[Notice] start == NULL");
        else if (end == NULL) {
            PWL_LOG_ERR("[Notice] end == NULL");

            // Check if rsp contain contain keywords
            if (strstr(rsp, "RMM-") ||
                strstr(rsp, "OP.") ||
                strstr(rsp, "OEM.") ||
                strstr(rsp, "DPV") ||
                strstr(rsp, "ESIM")) {
                if (strstr(rsp, "ESIM")) {
                    start = strstr(rsp, "ESIM");
                }
                strcpy(pcie_buffer, start);
                pcie_buffer[strcspn(pcie_buffer, "\n")] = 0;
                memset(buff_ptr, 0, strlen(pcie_buffer));
                strncpy(buff_ptr, pcie_buffer, strlen(pcie_buffer));
                return FALSE;
            } else {
                PWL_LOG_ERR("[Notice] rsp not include version keywords!!");
            }
        }
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
        // PWL_LOG_ERR("Response buffer size not large enough");
        // return FALSE;
        PWL_LOG_DEBUG("[Warning] Response buffer size not enough, message could be incomplete.");
        memset(buff_ptr, '\0', buff_size);
        strncpy(buff_ptr, start, buff_size - 1);
        if (DEBUG) PWL_LOG_DEBUG("%s", buff_ptr);
        return TRUE;
    }
}

pwl_cid_status_t at_cmd_request(gchar *command) {
    if (g_at_intf == PWL_AT_OVER_MBIM_API) {
        if (g_device_type == PWL_DEVICE_TYPE_USB) {
            pwl_mbimdeviceadpt_at_req(PWL_MBIM_AT_COMMAND, command, mbim_at_resp_cb);
        } else {
            pwl_mbimdeviceadpt_at_req(PWL_MBIM_AT_TUNNEL, command, mbim_at_resp_cb);
        }
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
            if (!at_resp_parsing(response, g_response, PWL_MQ_MAX_RESP)) {
                if (response) free(response);
                return PWL_CID_STATUS_ERROR;
            }
        } else {
            if (response) free(response);
            return PWL_CID_STATUS_ERROR;
        }
        if (response) {
            free(response);
            return PWL_CID_STATUS_OK;
        }
        return PWL_CID_STATUS_ERROR;
    }
    return PWL_CID_STATUS_ERROR;
}

pwl_cid_status_t madpt_at_cmd_request(gchar *command) {
    pwl_cid_status_t status;
    status = at_cmd_request(command);
    if (g_at_intf == PWL_AT_OVER_MBIM_API) {
        mbim_error_check();
    }
    return status;
}

static void mbim_device_ready_cb(gboolean opened) {
    if (opened) {
        //Send signal to pwl_pref to get fw version
        pwl_core_call_madpt_ready_method (gp_proxy, NULL, NULL, NULL);
    }
}

void mbim_error_check() {
    if (g_at_intf == PWL_AT_OVER_MBIM_API) {
       if (mbim_err_cnt >= PWL_MBIM_ERR_MAX) {
           pwl_mbimdeviceadpt_deinit();
           pwl_mbimdeviceadpt_init(NULL);
           sleep(3);
           mbim_err_cnt = 0;
       }
   }
}

static void mbim_at_resp_cb(const gchar* response) {
    if (strcmp(response, "ERROR") == 0) {
        mbim_err_cnt = PWL_MBIM_ERR_MAX;
    } else if (strcmp(response, "QUERY ERROR") == 0) {
        mbim_err_cnt++;
    } else {
        at_resp_parsing(response, g_response, PWL_MQ_MAX_RESP);
        pthread_cond_signal(&g_cond);
    }
}

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

static gboolean signal_notice_module_recovery_finish_handler(pwlCore *object, int arg_type, gpointer userdata) {
   if (NULL != g_signal_callback.callback_notice_module_recovery_finish) {
       g_signal_callback.callback_notice_module_recovery_finish(arg_type);
   }

   return TRUE;
}

gboolean register_client_signal_handler(pwlCore *p_proxy) {
    PWL_LOG_DEBUG("register_client_signal_handler call.");
    g_ret_signal_handler[0] = g_signal_connect(p_proxy, "notify::g-name-owner", G_CALLBACK(cb_owner_name_changed_notify), NULL);
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

gboolean mbim_init(gboolean boot) {
    mbim_err_cnt = 0;

    mbim_device_ready_callback cb = NULL;
    if (boot) {
        cb = mbim_device_ready_cb;
    }

    while (!pwl_mbimdeviceadpt_init(cb)) {
        sleep(5);
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
    gboolean has_flash_oem_img = FALSE;
    gboolean efs_recovery_mode = FALSE;
    /* create the message queue */
    mq = mq_open(PWL_MQ_PATH_MADPT, O_CREAT | O_RDONLY, 0644, &attr);

    while (1) {
        ssize_t bytes_read;

        /* receive the message */
        bytes_read = mq_receive(mq, (gchar *)&message, sizeof(message), NULL);

        print_message_info(&message);

        gboolean timedwait = TRUE;
        status = PWL_CID_STATUS_OK;
        memset(g_response, 0, PWL_MQ_MAX_RESP);

        if (message.pwl_cid == PWL_CID_MADPT_RESTART) {
            has_flash_oem_img = FALSE;
            efs_recovery_mode = FALSE;
            if (strcmp(message.content, "TRUE") == 0) {
                has_flash_oem_img = TRUE;
                efs_recovery_mode = FALSE;
            } else if (strcmp(message.content, "RECOVERY") == 0) {
                has_flash_oem_img = TRUE;
                efs_recovery_mode = TRUE;
            } else {
                has_flash_oem_img = FALSE;
                efs_recovery_mode = FALSE;
            }
            PWL_LOG_INFO("Has flash oem pri image: %d", has_flash_oem_img);
            PWL_LOG_INFO("message.content: %s, IS EFS recovery mode: %d", message.content, efs_recovery_mode);
            jp_fcc_config(FALSE, has_flash_oem_img, efs_recovery_mode);
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
        } else if (message.pwl_cid == PWL_CID_SETUP_JP_FCC_CONFIG) {
            enable_jp_fcc_auto_reboot();
            timedwait = FALSE;
        } else if (message.pwl_cid == PWL_CID_RESTORE_SN ||
                   message.pwl_cid == PWL_CID_RESTORE_IMEI) {
            cmd_len = strlen(at_cmd_map[ATCMD_INDEX_MAP(message.pwl_cid)]) + strlen(message.content) + 3;
            cust_set_cmd = (char *) malloc(cmd_len);
            if (!cust_set_cmd) {
                PWL_LOG_ERR("malloc failed for cust_set_cmd");
                return NULL;
            }
            memset(cust_set_cmd, '0', cmd_len);

            snprintf(cust_set_cmd, cmd_len, "%s\"%s\"", at_cmd_map[ATCMD_INDEX_MAP(message.pwl_cid)], message.content);
            if (DEBUG) PWL_LOG_DEBUG("Restore_cmd: %s", cust_set_cmd);
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

        if (g_at_intf == PWL_AT_OVER_MBIM_API) {
            if (mbim_err_cnt >= PWL_MBIM_ERR_MAX) {
                pwl_mbimdeviceadpt_deinit();
                pwl_mbimdeviceadpt_init(mbim_device_ready_cb);
                mbim_err_cnt = 0;
            }
        }

        PWL_LOG_INFO("total error (%d)", mbim_err_cnt);

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
            case PWL_CID_GET_PCIE_DEVICE_VERSION:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_GET_PCIE_AP_VERSION:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
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
            case PWL_CID_GET_PCIE_OP_VERSION:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_GET_PCIE_OEM_VERSION:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_GET_PCIE_DPV_VERSION:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_GET_CARRIER_ID:
                if (strlen(g_response) > 0) {
                    if (strstr(g_response, "ESBP")) {
                        PWL_LOG_DEBUG("Parse carrier id fom SBP.");
                        char *id;
                        int index = 0;
                        id = strtok(g_response, ",");
                        if (id != NULL) {
                            while (id != NULL) {
                                index++;
                                id = strtok(NULL, ",");
                                if (index == 1)
                                    break;
                            }
                            send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, id);
                        } else {
                            PWL_LOG_ERR("SBP response format not correct, can't parse carrier id");
                            send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, PWL_CID_STATUS_ERROR, "");
                        }
                    } else {
                        PWL_LOG_ERR("SBP response format not correct, can't parse carrier id");
                        send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, PWL_CID_STATUS_ERROR, "");
                    }
                } else {
                    PWL_LOG_ERR("Can't get sim SBP id, clear carrier id.");
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, PWL_CID_STATUS_ERROR, "");
                }
                break;
            case PWL_CID_GET_OEM_PRI_RESET_STATE:
                PWL_LOG_DEBUG("PWL_CID_GET_OEM_PRI_RESET_STATE, g_response: %s", g_response);
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_GET_MODULE_SKU_ID:
                if (strlen(g_response) > 0) {
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                } else {
                    PWL_LOG_ERR("Can't get module SKU ID");
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, PWL_CID_STATUS_ERROR, g_response);
                }
                break;
            case PWL_CID_SETUP_JP_FCC_CONFIG:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, "");
                break;
            case PWL_CID_GET_ESIM_STATE:
                PWL_LOG_DEBUG("[DPV] esim: %s", g_response);
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_CHECK_ESIM_TEST_PROF:
                if (DEBUG) PWL_LOG_DEBUG("Check eSIM test profile");
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_DELETE_ESIM_TEST_PROF:
                if (DEBUG) PWL_LOG_DEBUG("Delete eSIM test profile");
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_RESTORE_SN:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            case PWL_CID_RESTORE_IMEI:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_MADPT, message.sender_id, status, g_response);
                break;
            default:
                PWL_LOG_ERR("Unknown pwl cid: %d", message.pwl_cid);
                break;
        }
    }

    return NULL;
}

static gpointer jp_fcc_thread_func(gpointer data) {
    int jp_fcc_config_retry = 0;
    get_fw_update_status_value(JP_FCC_CONFIG_COUNT, &jp_fcc_config_retry);
    PWL_LOG_INFO("jp fcc config retry %d", jp_fcc_config_retry);
    if (jp_fcc_config_retry > 0 && jp_fcc_config_retry < JP_FCC_CONFIG_RETRY_TH) {
        jp_fcc_config_retry++;
        set_fw_update_status_value(JP_FCC_CONFIG_COUNT, jp_fcc_config_retry);
        jp_fcc_config(TRUE, FALSE, FALSE);
    } else {
        PWL_LOG_INFO("wait for modem to get ready");
        sleep(PWL_MBIM_READY_SEC);
    }

    send_message_reply(PWL_CID_MADPT_RESTART, PWL_MQ_ID_MADPT, PWL_MQ_ID_FWUPDATE, PWL_CID_STATUS_OK, "Madpt started");
    return NULL;
}

static gpointer recovery_wait_thread_func(gpointer data) {
    if (!cond_wait(&g_mutex, &g_cond, PWL_RECOVERY_CHECK_DELAY_SEC + 60)) {
        PWL_LOG_ERR("timed out for wait of recovery finish done, continue");
    } else {
        PWL_LOG_DEBUG("Recovery finished, madpt ready to work");
        sleep(1);
    }
    //Send signal to pwl_pref to get fw version
    pwl_core_call_madpt_ready_method (gp_proxy, NULL, NULL, NULL);

    send_message_reply(PWL_CID_MADPT_RESTART, PWL_MQ_ID_MADPT, PWL_MQ_ID_FWUPDATE, PWL_CID_STATUS_OK, "Madpt started");
    return NULL;
}

void enable_jp_fcc_auto_reboot() {
    PWL_LOG_DEBUG("Enable JP FCC auto reboot");
    // enable this flag so modem will do switch when sim changed to corresponding carrier
    pwl_get_enable_state_t state = PWL_CID_GET_ENABLE_STATE_ERROR;

    for (gint i = 0; i < PWL_OEM_PRI_RESET_RETRY; i++) {
        madpt_at_cmd_request(at_cmd_map[ATCMD_INDEX_MAP(PWL_CID_GET_JP_FCC_AUTO_REBOOT)]);

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
                madpt_at_cmd_request(at_cmd_map[ATCMD_INDEX_MAP(PWL_CID_ENABLE_JP_FCC_AUTO_REBOOT)]);

                if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
                    PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_ENABLE_JP_FCC_AUTO_REBOOT]);
                }

            } else if (state == PWL_CID_GET_ENABLE_STATE_ENABLED) {
                PWL_LOG_INFO("JP fcc auto reboot state enabled");
                set_fw_update_status_value(JP_FCC_CONFIG_COUNT, 0);
                return;
            }
            sleep(1);
        }
    }
}

void wait_for_modem_oem_pri_reset() {
    gint oem_reset_state = OEM_PRI_RESET_NOT_READY;

    for (gint i = 0; i < 20; i++) {
        sleep(3);
        madpt_at_cmd_request(at_cmd_map[ATCMD_INDEX_MAP(PWL_CID_GET_OEM_PRI_RESET)]);

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

void jp_fcc_config(gboolean enable_jp_fcc, gboolean has_flash_oem, gboolean efs_recovery_mode) {
    gboolean is_mbim_ready = FALSE;
    // just after flash, wait a bit for modem to get ready
    PWL_LOG_INFO("wait for modem to get ready");
    sleep(PWL_MBIM_READY_SEC);

    // Check mbim init (timout 5 mins 6s*50)
    for (int i = 0; i < 50; i++) {
        if (!mbim_init(FALSE)) {
            sleep(6);
            continue;
        } else {
            is_mbim_ready = TRUE;
            break;
        }
    }

    if (!is_mbim_ready) {
        PWL_LOG_ERR("Mbim init failed, do GPIO reset.");
        //gdbus_init();
        //while(!dbus_service_is_ready());
        //PWL_LOG_DEBUG("DBus Service is ready");
        pwl_core_call_gpio_reset_method_sync (gp_proxy, NULL, NULL);
        // hw reset function will send PWL_CID_MADPT_RESTART msg but can't receive at this point
        // so restart it self
        //g_exit_code = EXIT_FAILURE;
        restart();
        return;
    }

    sleep(5); // immediatly use of mbim will cause error, wait a bit

    if (enable_jp_fcc)
        enable_jp_fcc_auto_reboot();

    // Check oem pri info:
    // START(0)/NORESET(3) > do nothing
    // INIT(1) > wait
    // REST(2) > reset module
    if (has_flash_oem) {
        gint retry = 0;
        if (efs_recovery_mode) {
            PWL_LOG_DEBUG("Sleep 2 mins for efs recovery");
            sleep(60 * 2);
        }
        while (g_oem_pri_state != OEM_PRI_UPDATE_NORESET) {
            if (retry > 10) break;  // retry wait for OEM_PRI_UPDATE_RESET for 50s then exit

            PWL_LOG_DEBUG("===== Get OEM pri info =====, def: %d, retry: %d", g_oem_pri_state, retry);
            madpt_at_cmd_request(at_cmd_map[ATCMD_INDEX_MAP(PWL_CID_GET_OEM_PRI_INFO)]);
            sleep(5);
            retry++;
            g_oem_pri_state = atoi(g_response);
            PWL_LOG_DEBUG("===== OEM info int: %d", g_oem_pri_state);
            if (g_oem_pri_state == OEM_PRI_UPDATE_START || g_oem_pri_state == OEM_PPI_UPDATE_INIT) {
                continue;
            } else if (g_oem_pri_state == OEM_PRI_UPDATE_RESET) {
                PWL_LOG_DEBUG("===== Oem pri need reset =====");
                wait_for_modem_oem_pri_reset();
                break;
            } else {
                continue;
            }
        }
        PWL_LOG_DEBUG("retry times: %d", retry);
        PWL_LOG_DEBUG("===== OEM info check END =====");

        for (int i = 0; i < 5; i++) {
            madpt_at_cmd_request(at_cmd_map[ATCMD_INDEX_MAP(PWL_CID_RESET)]);
            if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
                PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_RESET]);
            } else {
                break;
            }
        }

        // wait till mbim port gone
        for (int i = 0; i < 10; i++) {
            gchar port[20];
            memset(port, 0, sizeof(port));
            if (!pwl_find_mbim_port(port, sizeof(port))) {
                break;
            }
            sleep(2);
        }
        PWL_LOG_INFO("mbim port gone now");

        if (pwl_mbimdeviceadpt_port_wait()) {
            PWL_LOG_INFO("mbim port available now");
        } else {
            PWL_LOG_INFO("mbim port still not available after wait");
        }
    }

    restart();
}

void clean_up() {
    if (g_at_intf == PWL_AT_OVER_MBIM_API) {
        pwl_mbimdeviceadpt_deinit();
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

void signal_callback_notice_module_recovery_finish(int type) {
    PWL_LOG_DEBUG("!!! signal_callback_notice_module_recovery_finish !!!");
    if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
        pthread_cond_signal(&g_cond);
    }
    return;
}

int check_if_mbim_api_exist() {
    void *handle;
    void *set_new_func = NULL;
    void *parse_func = NULL;

    handle = dlopen("libmbim-glib.so", RTLD_LAZY);
    if (!handle) {
        PWL_LOG_DEBUG("Can not open libmbim: %s", dlerror());
        PWL_LOG_DEBUG("Try to open libmbim-glib.so.4");
        handle = dlopen("libmbim-glib.so.4", RTLD_LAZY);
        if (!handle) {
            PWL_LOG_DEBUG("Can not open libmbim-glib.so.4: %s", dlerror());
            g_at_intf = PWL_AT_CHANNEL;
            return -1;
        }
    }

    dlerror();

    set_new_func = dlsym(handle, "mbim_message_intel_at_tunnel_at_command_set_new");
    parse_func = dlsym(handle, "mbim_message_intel_at_tunnel_at_command_response_parse");

    if (set_new_func && parse_func) {
        if (DEBUG) PWL_LOG_DEBUG("MBIM api check pass, set PWL_AT_OVER_MBIM_API");
        g_at_intf = PWL_AT_OVER_MBIM_API;
    } else {
        if (!set_new_func)
            if (DEBUG) PWL_LOG_DEBUG("No mbim_message_intel_at_tunnel_at_command_set_new API");
        if (!parse_func)
            if (DEBUG) PWL_LOG_DEBUG("No mbim_message_intel_at_tunnel_at_command_response_parse API");
        if (DEBUG) PWL_LOG_DEBUG("MBIM api check failed, set PWL_AT_CHANNEL");
        g_at_intf = PWL_AT_CHANNEL;
    }
    dlclose(handle);
    return 0;
}

gint main() {
    PWL_LOG_INFO("start");

    g_device_type = pwl_get_device_type_await();
    if (g_device_type == PWL_DEVICE_TYPE_UNKNOWN) {
        PWL_LOG_INFO("Unsupported device.");
        return EXIT_SUCCESS;
    }

    pwl_discard_old_messages(PWL_MQ_PATH_MADPT);

    // dynamic check if libmbim AT Tunnel API exist
    if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
        check_if_mbim_api_exist();
    }

    if (g_at_intf == PWL_AT_OVER_MBIM_API) {
        if (g_device_type == PWL_DEVICE_TYPE_USB) {
            mbim_init(TRUE);
        } else {
            // need to wait for recovery check to complete first, so no need
            // to register cb for signaling madpt_ready at this point
            mbim_init(FALSE);
        }
        sleep(5);
    }

    GThread *msg_queue_thread = g_thread_new("msg_queue_thread", msg_queue_thread_func, NULL);

    signal_callback_t signal_callback;
    signal_callback.callback_notice_module_recovery_finish = signal_callback_notice_module_recovery_finish;
    registerSignalCallback(&signal_callback);

    gdbus_init();

    while(!dbus_service_is_ready());
    PWL_LOG_DEBUG("DBus Service is ready");

    if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
        GThread *recovery_wait_thread = g_thread_new("recovery_wait_thread", recovery_wait_thread_func, NULL);
    } else if (g_device_type == PWL_DEVICE_TYPE_USB) {
        GThread *jp_fcc_thread = g_thread_new("jp_fcc_thread", jp_fcc_thread_func, NULL);
    }

    if (gp_loop != NULL)
        g_main_loop_run(gp_loop);

exit:

    clean_up();

    return g_exit_code;
}
