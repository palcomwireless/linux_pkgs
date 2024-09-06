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

#include <stdbool.h>
#include <stdio.h>
#include <sys/msg.h>
#include <string.h>

#include "common.h"
#include "dbus_common.h"
#include "log.h"
#include "pwl_pref.h"

// For general mutex usage
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;

static GMainLoop *gp_loop = NULL;
static pwlCore *gp_proxy = NULL;
static gulong g_ret_signal_handler[RET_SIGNAL_HANDLE_SIZE];
static signal_callback_t g_signal_callback;
//static method_callback_t g_method_callback;

//FW version info
static char *g_main_fw_version;
static char *g_ap_version;
static char *g_carrier_version;
static char *g_oem_version;
// For pcie device
static char *g_modem_version;
static char *g_op_version;
static char *g_oem_version;
static char *g_dpv_version;

//SIM info
static int g_mnc_len;
static char g_sim_carrier[15];
static char g_pref_carrier[MAX_PATH];
static bool g_is_get_cimi = FALSE;
gboolean g_is_sim_insert = FALSE;
gboolean g_set_pref_carrier_ret = FALSE;
gboolean g_check_sim_carrier_on_going = FALSE;

static void cb_owner_name_changed_notify(GObject *object, GParamSpec *pspec, gpointer userdata);
static bool register_client_signal_handler(pwlCore *p_proxy);

static gchar g_manufacturer[PWL_MAX_MFR_SIZE] = {0};
static gchar g_skuid[PWL_MAX_SKUID_SIZE] = {0};
static gchar g_fwver[INFO_BUFFER_SIZE] = {0};

static pwl_device_type_t g_device_type = PWL_DEVICE_TYPE_UNKNOWN;

static gboolean signal_get_fw_version_handler(pwlCore *object, const gchar *arg, gpointer userdata) {
    if (NULL != g_signal_callback.callback_get_fw_version) {
        g_signal_callback.callback_get_fw_version(arg);
    }

    return TRUE;
}

static gboolean signal_sim_state_change_handler(pwlCore *object, gint arg_status, gpointer userdata) {
    PWL_LOG_DEBUG("signal_sim_state_change_handler");
    if (NULL != g_signal_callback.callback_sim_state_change) {
        g_signal_callback.callback_sim_state_change(arg_status);
    }

    return TRUE;
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

bool register_client_signal_handler(pwlCore *p_proxy) {
    PWL_LOG_DEBUG("register_client_signal_handler call.");
    g_ret_signal_handler[0] = g_signal_connect(p_proxy, "notify::g-name-owner", G_CALLBACK(cb_owner_name_changed_notify), NULL);
    g_ret_signal_handler[1] = g_signal_connect(p_proxy, "get-fw-version-signal", G_CALLBACK(signal_get_fw_version_handler), NULL);
    g_ret_signal_handler[2] = g_signal_connect(p_proxy, "subscriber-ready-state-change",
                                               G_CALLBACK(signal_sim_state_change_handler), NULL);
    return TRUE;
}

void registerSignalCallback(signal_callback_t *callback) {
    if (NULL != callback) {
        memcpy(&g_signal_callback, callback, sizeof(signal_callback_t));
    } else {
        PWL_LOG_DEBUG("registerSignalCallback: parameter point is NULL");
    }
}

//void registerMethodCallback(method_callback_t *callback) {
//    if (NULL != callback) {
//        memcpy(&g_method_callback, callback, sizeof(method_callback_t));
//    } else {
//        PWL_LOG_DEBUG("registerMethodCallback: parameter point is NULL");
//    }
//}

bool gdbus_init(void) {
    bool b_ret = TRUE;
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
        return true;
    } else {
        PWL_LOG_ERR("Owner Name is NULL.");
        sleep(1);
        gdbus_init();
        return false;
    }
}

void send_message_queue(uint32_t cid) {
    mqd_t mq;
    mq = mq_open(CID_DESTINATION(cid), O_WRONLY);

    // message to be sent
    msg_buffer_t message;
    message.pwl_cid = cid;
    message.status = PWL_CID_STATUS_NONE;
    message.sender_id = PWL_MQ_ID_PREF;

    // msgsnd to send message
    mq_send(mq, (gchar *)&message, sizeof(message), 0);
}

void signal_callback_get_fw_version(const gchar* arg) {
    PWL_LOG_DEBUG("!!! signal_callback_get_fw_version !!!");
    g_check_sim_carrier_on_going = TRUE;

    if (g_device_type == PWL_DEVICE_TYPE_USB) {
        for (int i = 0; i < 3; i++) {
            send_message_queue(PWL_CID_GET_FW_VER);
            if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
                PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_GET_FW_VER]);
            }
            if (g_main_fw_version != NULL && strlen(g_main_fw_version) > 0) {
                break;
            } else {
                g_usleep(1000*300);
                continue;
            }
        }
    } else if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
        // Using ati command to get ap version
        // Get AP and MD version
        for (int i = 0; i < 3; i++) {
            send_message_queue(PWL_CID_GET_PCIE_DEVICE_VERSION);
            if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
                PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_GET_FW_VER]);
            }
            if (g_ap_version != NULL && strlen(g_ap_version) > 0) {
                break;
            } else {
                g_usleep(1000*300);
                continue;
            }
        }
        // Get OP version
        for (int i = 0; i < 3; i++) {
            send_message_queue(PWL_CID_GET_PCIE_OP_VERSION);
            if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
                PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_GET_PCIE_OP_VERSION]);
            }
            if (g_op_version != NULL && strlen(g_op_version) > 0) {
                break;
            } else {
                g_usleep(1000*500);
                continue;
            }
        }
        // Get OEM version
        for (int i = 0; i < 3; i++) {
            send_message_queue(PWL_CID_GET_PCIE_OEM_VERSION);
            if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
                PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_GET_PCIE_OEM_VERSION]);
            }
            if (g_oem_version != NULL && strlen(g_oem_version) > 0) {
                break;
            } else {
                g_usleep(1000*500);
                continue;
            }
        }
        // Get DPV version
        for (int i = 0; i < 3; i++) {
            send_message_queue(PWL_CID_GET_PCIE_DPV_VERSION);
            if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
                PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_GET_PCIE_DPV_VERSION]);
            }
            if (g_dpv_version != NULL && strlen(g_dpv_version) > 0) {
                break;
            } else {
                g_usleep(1000*500);
                continue;
            }
        }
    }

    for (int i = 0; i < 3; i++) {
        send_message_queue(PWL_CID_GET_CRSM);
        if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
            PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_GET_CRSM]);
        }
        if (g_mnc_len == 0) {
            g_usleep(1000*300);
            continue;
        } else {
            for (int j = 0; j < 3; j++) {
                send_message_queue(PWL_CID_GET_CIMI);
                if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
                    PWL_LOG_ERR("timed out or error for cid %s", cid_name[PWL_CID_GET_CIMI]);
                }
                if (strlen(g_sim_carrier) > 0) {
                    if (g_device_type == PWL_DEVICE_TYPE_USB) {
                        if (get_preferred_carrier() == 0) {
                            // Compare sim carrier and preferred carrier
                            PWL_LOG_DEBUG("Compare sim carrier: %s, pref carrier: %s", g_sim_carrier, g_pref_carrier);
                            if (strncasecmp(g_sim_carrier, g_pref_carrier, strlen(g_sim_carrier)) != 0) {
                                PWL_LOG_DEBUG("Set preferred carrier to: %s", g_sim_carrier);
                                set_preferred_carrier(g_sim_carrier, 3);
                            }
                        }
                    }
                    g_check_sim_carrier_on_going = FALSE;
                    break;
                }
                g_usleep(1000*300);
            }
            g_check_sim_carrier_on_going = FALSE;
            break;
        }
    }
    g_check_sim_carrier_on_going = FALSE;
    return;
}

gint get_sim_carrier_info(int retry_delay, int retry_limit) {
    int err, retry = 0;

    // Get mnc length from CRSM
    sleep(3);
    while (retry < retry_limit) {
        // Abort when sim not insert
        if (!g_is_sim_insert) {
            g_check_sim_carrier_on_going = FALSE;
            return -1;
        }

        err = 0;
        g_is_get_cimi = FALSE;
        g_mnc_len = 0;
        PWL_LOG_DEBUG("Get CRSM");
        send_message_queue(PWL_CID_GET_CRSM);
        if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
            PWL_LOG_ERR("Time out to get CRSM, retry");
            err = 1;
        }
        if (err || g_mnc_len <= 0) {
            PWL_LOG_DEBUG("Get CRSM error, retry!");
            retry++;
            sleep(retry_delay);
            continue;
        }
        break;
    }

    // Get sim carrier
    sleep(3);
    while (retry < retry_limit) {
        if (!g_is_sim_insert) {
            g_check_sim_carrier_on_going = FALSE;
            return -1;
        }
        err = 0;
        g_is_get_cimi = FALSE;
        PWL_LOG_DEBUG("Get CIMI");
        send_message_queue(PWL_CID_GET_CIMI);
        if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
            PWL_LOG_ERR("Time out to get CIMI, retry");
            err = 1;
        }
        if (err || !g_is_get_cimi) {
            PWL_LOG_DEBUG("Get CIMI error, retry!");
            retry++;
            sleep(retry_delay);
            continue;
        }
        g_check_sim_carrier_on_going = FALSE;
        return 0;
    }
    g_check_sim_carrier_on_going = FALSE;
    return -1;
}

gint get_preferred_carrier()
{
    int err, retry = 0;
    memset(g_pref_carrier, 0, sizeof(g_pref_carrier));
    sleep(3);
    while (retry < PWL_FW_UPDATE_RETRY_LIMIT)
    {
        err = 0;
        send_message_queue(PWL_CID_GET_PREF_CARRIER);
        if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
            PWL_LOG_ERR("Time out to get pref carrier, retry");
            err = 1;
        }

        if (err || strlen(g_pref_carrier) == 0) {
            retry++;
            continue;
        }
        return 0;
    }
    return -1;
}

void send_message_queue_with_content(uint32_t cid, char *content) {
    mqd_t mq;
    mq = mq_open(CID_DESTINATION(cid), O_WRONLY);

    // message to be sent
    msg_buffer_t message;
    message.pwl_cid = cid;
    message.status = PWL_CID_STATUS_NONE;
    message.sender_id = PWL_MQ_ID_PREF;
    strcpy(message.content, content);

    // msgsnd to send message
    mq_send(mq, (gchar *)&message, sizeof(message), 0);
}

gint set_preferred_carrier(char *carrier, int retry_limit) {
    int err, retry = 0;

    while (retry < retry_limit) {
        err = 0;
        g_set_pref_carrier_ret = FALSE;
        send_message_queue_with_content(PWL_CID_SET_PREF_CARRIER, carrier);
        if (!cond_wait(&g_mutex, &g_cond, PWL_CMD_TIMEOUT_SEC)) {
            PWL_LOG_ERR("Time out to get pref carrier, retry");
            err = 1;
        }

        if (err || !g_set_pref_carrier_ret) {
            retry++;
            sleep(1);
            continue;
        }
        return 0;
    }
    return -1;
}

void signal_callback_sim_state_change(gint ready_state) {
    // PWL_LOG_DEBUG("!!! signal_callback_sim_state_change, arg: %d", arg);

    if (g_device_type != PWL_DEVICE_TYPE_USB)
        return;

    switch (ready_state) {
        case PWL_SIM_STATE_INITIALIZED:
            PWL_LOG_DEBUG("Sim insert");
            g_is_sim_insert = TRUE;
            if (!g_check_sim_carrier_on_going) {
                g_check_sim_carrier_on_going = TRUE;

                // Try to get sim carrier
                if (get_sim_carrier_info(PWL_PREF_GET_SIM_INFO_DELAY, PWL_PREF_CMD_RETRY_LIMIT) == 0) {
                    if (get_preferred_carrier() == 0) {
                        // Compare sim carrier and preferred carrier
                        PWL_LOG_DEBUG("Compare sim carrier: %s, pref carrier: %s", g_sim_carrier, g_pref_carrier);
                        if (strncasecmp(g_sim_carrier, g_pref_carrier, strlen(g_sim_carrier)) != 0) {
                            PWL_LOG_DEBUG("Set preferred carrier to: %s", g_sim_carrier);
                            set_preferred_carrier(g_sim_carrier, PWL_PREF_SET_CARRIER_RETRY_LIMIT);
                            g_check_sim_carrier_on_going = FALSE;
                        }
                    }
                } else {
                    PWL_LOG_DEBUG("Sim not insert, abort get carrier.");
                }
            } else {
                PWL_LOG_DEBUG("Sim check on going: %d", g_check_sim_carrier_on_going);
                PWL_LOG_DEBUG("Switch carrier abort!");
            }
            break;
        case PWL_SIM_STATE_NOT_INSERTED:
            PWL_LOG_DEBUG("Sim eject");
            g_is_sim_insert = FALSE;
            g_check_sim_carrier_on_going = FALSE;
            break;
        default:
            PWL_LOG_ERR("ready state unknown");
            break;
    }
    return;
}

void get_carrier_from_sim(char *mcc, char *mnc)
{
    int line, i, found = 0;
    char buffer[255];
    char *splitted_str;
    char *carrier_config[4];
    char carrier_name[15];

    if (DEBUG)
    {
        PWL_LOG_DEBUG("mcc: %s", mcc);
        PWL_LOG_DEBUG("mnc: %s", mnc);
    }

    FILE *file = fopen("/opt/pwl/mcc_mnc_list.csv", "r");
    if (file)
    {
        while (fgets(buffer, 255, file))
        {
            line++;
            // Skip title
            if (line == 1)
                continue;
            i = 0;
            splitted_str = strtok(buffer, ",");
            while (splitted_str != NULL)
            {
                carrier_config[i] = splitted_str;
                splitted_str = strtok(NULL, ",");
                i++;
            }
            // Check mcc
            if (strcmp(mcc, carrier_config[0]) != 0)
                continue;
            else
            {
                // Check mnc
                if (strcmp(mnc, carrier_config[1]) != 0)
                    continue;
                else
                {
                    strcpy(carrier_name, carrier_config[2]);
                    PWL_LOG_DEBUG("carrier_name: %s", carrier_name);
                    found = 1;
                    break;
                }
            }
        }
        if (found)
            strcpy(g_sim_carrier, carrier_name);
        else
        {
            strcpy(g_sim_carrier, "Generic");
        }
        fclose(file);
    }
    else
        strcpy(g_sim_carrier, PWL_UNKNOWN_SIM_CARRIER);
}

static gpointer msg_queue_thread_func(gpointer data) {
    mqd_t mq;
    struct mq_attr attr;
    msg_buffer_t message;

    /* initialize the queue attributes */
    attr.mq_flags = 0;
    attr.mq_maxmsg = PWL_MQ_MAX_MSG;
    attr.mq_msgsize = sizeof(message);
    attr.mq_curmsgs = 0;
    char mnc_len_str[2];
    char sim_mcc_mnc[2][4];
    /* create the message queue */
    mq = mq_open(PWL_MQ_PATH_PREF, O_CREAT | O_RDONLY, 0644, &attr);

    while (1) {
        ssize_t bytes_read;

        /* receive the message */
        bytes_read = mq_receive(mq, (gchar *)&message, sizeof(message), NULL);

        print_message_info(&message);

        switch (message.pwl_cid)
        {
            /* CID request from others */
            case PWL_CID_GET_MFR:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_OK, g_manufacturer);
                break;
            case PWL_CID_GET_SKUID:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_OK, g_skuid);
                break;
            case PWL_CID_GET_MAIN_FW_VER:
                if (g_main_fw_version != NULL)
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_OK, g_main_fw_version);
                else {
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_ERROR, "");
                    PWL_LOG_ERR("Main FW version not ready!");
                }
                break;
            case PWL_CID_GET_AP_VER:
                if (g_ap_version != NULL)
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_OK, g_ap_version);
                else {
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_ERROR, "");
                    PWL_LOG_ERR("AP version not ready!");
                }
                break;
            case PWL_CID_GET_MD_VER:
                if (g_modem_version != NULL)
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_OK, g_modem_version);
                else {
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_ERROR, "");
                    PWL_LOG_ERR("Modem version not ready!");
                }
                break;
            case PWL_CID_GET_OP_VER:
                if (g_op_version != NULL)
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_OK, g_op_version);
                else {
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_ERROR, "");
                    PWL_LOG_ERR("OP version not ready!");
                }
                break;
            case PWL_CID_GET_OEM_VER:
                if (g_oem_version != NULL)
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_OK, g_oem_version);
                else {
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_ERROR, "");
                    PWL_LOG_ERR("OEM version not ready!");
                }
                break;
            case PWL_CID_GET_DPV_VER:
                if (g_dpv_version != NULL)
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_OK, g_dpv_version);
                else {
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_ERROR, "");
                    PWL_LOG_ERR("DPV version not ready!");
                }
                break;
            case PWL_CID_UPDATE_FW_VER:
                PWL_LOG_DEBUG("Receive update req, send req msg.");
                send_message_queue(PWL_CID_GET_FW_VER);
                break;
            case PWL_CID_GET_CRSM:
                g_mnc_len = 0;
                if (message.status == PWL_CID_STATUS_OK) {
                    if (strlen(message.response) > 0 && strncmp("+CRSM: ", message.response, strlen("+CRSM: ")) == 0)
                    {
                        // search first '"'
                        gchar* match = strstr(message.response, "\"");
                        if (match != NULL) {
                            // search 2nd '"'
                            match = strstr(match + 1, "\"");
                        }
                        if (match != NULL) {
                            match--;
                            memset(mnc_len_str, 0, sizeof(mnc_len_str));
                            strncpy(mnc_len_str, match, 1);
                            g_mnc_len = atoi(mnc_len_str);
                            if (g_mnc_len > 3) g_mnc_len = 0;
                        }
                    }
                }
                if (DEBUG) PWL_LOG_DEBUG("g_mnc_len: %d", g_mnc_len);
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_CIMI:
                if (message.status == PWL_CID_STATUS_OK) {
                    if (DEBUG) PWL_LOG_DEBUG("%s", message.response);
                    memset(sim_mcc_mnc, 0, sizeof(sim_mcc_mnc));
                    strncpy(&sim_mcc_mnc[0][0], message.response, 3);
                    char *mnc = message.response + 3;
                    strncpy(&sim_mcc_mnc[1][0], mnc, g_mnc_len);
                    get_carrier_from_sim(sim_mcc_mnc[0], sim_mcc_mnc[1]);
                    PWL_LOG_DEBUG("Sim carrier: %s", g_sim_carrier);
                    g_is_get_cimi = TRUE;
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_SIM_CARRIER:
                if (strlen(g_sim_carrier) > 0) {
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_OK, g_sim_carrier);
                } else {
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_ERROR, g_sim_carrier);
                }
                break;
            /* CID request from myself */
            case PWL_CID_GET_FW_VER:
                if (message.status == PWL_CID_STATUS_OK && strlen(message.response) > 0)
                {
                    strcpy(g_fwver, message.response);
                    // PWL_LOG_DEBUG("g_fwver: %s", g_fwver);
                    split_fw_versions(message.response);
                }
                else
                    PWL_LOG_DEBUG("FW version abnormal, abort!");
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_PCIE_DEVICE_VERSION:
                if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
                    if ((message.status == PWL_CID_STATUS_OK || message.status == PWL_CID_STATUS_ERROR) &&
                        strlen(message.response) > 0 &&
                        strstr(message.response, "RMM")) {
                        split_pcie_device_versions(message.response);
                    }
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_PCIE_OP_VERSION:
                if (g_device_type == PWL_DEVICE_TYPE_PCIE ) {
                    if ((message.status == PWL_CID_STATUS_OK || message.status == PWL_CID_STATUS_ERROR) &&
                        strlen(message.response) > 0 &&
                        strstr(message.response, "OP.")) {

                        int index = 0;
                        char *splitted_str;
                        if (g_op_version) {
                            free(g_op_version);
                            g_op_version = NULL;
                        }
                        g_op_version = malloc(MAX_PCIE_VERSION_LENGTH);
                        memset(g_op_version, 0, sizeof(g_op_version));
                        splitted_str = strtok(message.response, " ");
                        while (splitted_str != NULL) {
                            splitted_str = strtok(NULL, " ");
                            if (splitted_str != NULL) {
                                strcpy(g_op_version, splitted_str);
                                g_op_version[strcspn(g_op_version, "\n")] = 0;
                                PWL_LOG_DEBUG("OP Version: %s", g_op_version);
                                break;
                            }
                        }
                    }
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_PCIE_OEM_VERSION:
                if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
                    if ((message.status == PWL_CID_STATUS_OK || message.status == PWL_CID_STATUS_ERROR) &&
                        strlen(message.response) > 0 &&
                        strstr(message.response, "OEM.")) {

                        int index = 0;
                        char *splitted_str;
                        if (g_oem_version) {
                            free(g_oem_version);
                            g_oem_version = NULL;
                        }
                        g_oem_version = malloc(MAX_PCIE_VERSION_LENGTH);
                        memset(g_oem_version, 0, sizeof(g_oem_version));
                        splitted_str = strtok(message.response, " ");
                        while (splitted_str != NULL) {
                            splitted_str = strtok(NULL, " ");
                            if (splitted_str != NULL) {
                                strcpy(g_oem_version, splitted_str);
                                g_oem_version[strcspn(g_oem_version, "\n")] = 0;
                                PWL_LOG_DEBUG("OEM Version: %s", g_oem_version);
                                break;
                            }
                        }
                    }
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_GET_PCIE_DPV_VERSION:
                if (g_device_type == PWL_DEVICE_TYPE_PCIE) {
                    if ((message.status == PWL_CID_STATUS_OK || message.status == PWL_CID_STATUS_ERROR) &&
                        strlen(message.response) > 0 &&
                        strstr(message.response, "DPV")) {

                        int index = 0;
                        char *splitted_str;
                        if (g_dpv_version) {
                            free(g_dpv_version);
                            g_dpv_version = NULL;
                        }
                        g_dpv_version = malloc(MAX_PCIE_VERSION_LENGTH);
                        memset(g_dpv_version, 0, sizeof(g_dpv_version));
                        splitted_str = strtok(message.response, " ");
                        while (splitted_str != NULL) {
                            splitted_str = strtok(NULL, " ");
                            if (splitted_str != NULL) {
                                strcpy(g_dpv_version, splitted_str);
                                g_dpv_version[strcspn(g_dpv_version, "\n")] = 0;
                                PWL_LOG_DEBUG("DPV Version: %s", g_dpv_version);
                                break;
                            }
                        }
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
                        // for (int n = 0; n < MAX_PREFERRED_CARRIER_NUMBER; n++) {
                        //     if (DEBUG) PWL_LOG_DEBUG("Compare: %s with %s", g_pref_carrier, g_preferred_carriers[n]);
                        //     if (strcasecmp(g_pref_carrier, g_preferred_carriers[n]) == 0)
                        //         g_pref_carrier_id = n;
                        // }
                        PWL_LOG_DEBUG("Preferred carrier: %s", g_pref_carrier);
                    } else {
                        PWL_LOG_ERR("Can't find preferred carrier!");
                    }
                }
                pthread_cond_signal(&g_cond);
                break;
            case PWL_CID_SET_PREF_CARRIER:
                if (message.status == PWL_CID_STATUS_OK) {
                    g_set_pref_carrier_ret = TRUE;
                    if (DEBUG) PWL_LOG_DEBUG("Set preferred carrier result: %s", message.response);
                }
                pthread_cond_signal(&g_cond);
                break;
            default:
                PWL_LOG_ERR("Unknown pwl cid: %d", message.pwl_cid);
                break;
        }
    }

    return NULL;
}

void split_pcie_device_versions(char *sw_version) {
    char *splitted_str;
    char *temp;
    int index = 0;
    char full_version[MAX_PCIE_VERSION_LENGTH] = {0};

    if (g_modem_version) {
        free(g_modem_version);
        g_modem_version = NULL;
    }
    if (g_ap_version) {
        free(g_ap_version);
        g_ap_version = NULL;
    }

    if (sw_version == NULL || strlen(sw_version) <= 0) {
        PWL_LOG_ERR("sw_version abnormal, split failed!");
        return;
    }

    // Remove "SW VERSION"
    temp = strstr(sw_version, "RMM");
    if (DEBUG) {
        PWL_LOG_DEBUG("sw_version: %s", sw_version);
        PWL_LOG_DEBUG("temp: %s, index: %d, strlen: %ld", temp, index, strlen(sw_version));
    }
    if (temp) {
        index = temp - sw_version;
    } else {
        PWL_LOG_ERR("sw_version not include fw version, split failed!");
        return;
    }

    strncpy(full_version, sw_version+index, strlen(sw_version) - index);
    if(DEBUG) PWL_LOG_DEBUG("sw version: %s", full_version);

    // Split modem and ap version
    g_modem_version = malloc(MAX_PCIE_VERSION_LENGTH);
    memset(g_modem_version, 0, MAX_PCIE_VERSION_LENGTH);
    g_ap_version = malloc(MAX_PCIE_AP_VERSION_LENGTH);
    memset(g_ap_version, 0, MAX_PCIE_AP_VERSION_LENGTH);
    index = 0;
    splitted_str = strtok(full_version, "_");
    if (splitted_str != NULL)
        strcpy(g_modem_version, splitted_str);

    while(splitted_str != NULL) {
        splitted_str = strtok(NULL, "_");
        if (splitted_str != NULL) {
            strcpy(g_ap_version, splitted_str);
            g_ap_version[strcspn(g_ap_version, "\n")] = 0;
            break;
        }
    }
    PWL_LOG_DEBUG("modem version: %s", g_modem_version);
    PWL_LOG_DEBUG("ap version: %s", g_ap_version);
}

void split_fw_versions(char *fw_version) {
    char *splitted_str;
    char *version_array[4] = { NULL };
    int i = 0;

    PWL_LOG_DEBUG("fw version %s", fw_version);

    if (g_main_fw_version) {
        free(g_main_fw_version);
        g_main_fw_version = NULL;
    }
    if (g_ap_version) {
        free(g_ap_version);
        g_ap_version = NULL;
    }
    if (g_carrier_version) {
        free(g_carrier_version);
        g_carrier_version = NULL;
    }
    if (g_oem_version) {
        free(g_oem_version);
        g_oem_version = NULL;
    }

    splitted_str = strtok(fw_version, "_");
    while (i < 4)
    {
        version_array[i] = splitted_str;
        splitted_str = strtok(NULL, "_");
        i++;
    }

    if (version_array[0] != NULL && version_array[1] != NULL &&
        version_array[2] != NULL && version_array[3] != NULL) {
        g_main_fw_version = (char *) malloc(strlen(version_array[0]) + 1);
        g_ap_version = (char *) malloc(strlen(version_array[1]) + 1);
        g_carrier_version = (char *) malloc(strlen(version_array[2]) + 1);
        g_oem_version = (char *) malloc(strlen(version_array[3]) + 1);
        strcpy(g_main_fw_version, version_array[0]);
        strcpy(g_ap_version, version_array[1]);
        strcpy(g_carrier_version, version_array[2]);
        strcpy(g_oem_version, version_array[3]);
    }

    PWL_LOG_DEBUG("main fw version: %s", g_main_fw_version);
    PWL_LOG_DEBUG("ap version: %s", g_ap_version);
    PWL_LOG_DEBUG("carrier version: %s", g_carrier_version);
    PWL_LOG_DEBUG("oem version: %s", g_oem_version);
}

gint main() {
    g_device_type = pwl_get_device_type_await();
    if (g_device_type == PWL_DEVICE_TYPE_UNKNOWN) {
        PWL_LOG_INFO("Unsupported device.");
        return 0;
    }

    pwl_discard_old_messages(PWL_MQ_PATH_PREF);

    pwl_get_manufacturer(g_manufacturer, PWL_MAX_MFR_SIZE);
    if (DEBUG) PWL_LOG_DEBUG("Manufacturer: %s", g_manufacturer);
    pwl_get_skuid(g_skuid, PWL_MAX_SKUID_SIZE);
    if (DEBUG) PWL_LOG_DEBUG("SKU Number: %s", g_skuid);

    signal_callback_t signal_callback;

    signal_callback.callback_get_fw_version = signal_callback_get_fw_version;
    signal_callback.callback_sim_state_change = signal_callback_sim_state_change;

    registerSignalCallback(&signal_callback);


    gdbus_init();

    GThread *msg_queue_thread = g_thread_new("msg_queue_thread", msg_queue_thread_func, NULL);

    while(!dbus_service_is_ready());

    if (gp_loop != NULL)
        g_main_loop_run(gp_loop);

    if (0 != gp_loop) {
        g_main_loop_quit(gp_loop);
        g_main_loop_unref(gp_loop);
    }

    return 0;
}
