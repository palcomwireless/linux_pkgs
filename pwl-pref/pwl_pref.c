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

#define INFO_BUFFER_SIZE            100

static GMainLoop *gp_loop = NULL;
static pwlCore *gp_proxy = NULL;
static gulong g_ret_signal_handler[RET_SIGNAL_HANDLE_SIZE];
static signal_callback_t g_signal_callback;
//static method_callback_t g_method_callback;

//FW version info
static char* g_main_fw_version;
static char* g_ap_version;
static char* g_carrier_version;
static char* g_oem_version;

//SIM info
static int g_mnc_len;
static char g_sim_carrier[15];

static void cb_owner_name_changed_notify(GObject *object, GParamSpec *pspec, gpointer userdata);
static bool register_client_signal_handler(pwlCore *p_proxy);

static gchar g_manufacturer[PWL_MAX_MFR_SIZE] = {0};
static gchar g_skuid[PWL_MAX_SKUID_SIZE] = {0};
static gchar g_fwver[INFO_BUFFER_SIZE] = {0};

static gboolean get_host_info(const gchar *cmd, gchar *buff, gint buff_len) {
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        PWL_LOG_ERR("cmd error!!!");
        return FALSE;
    }

    fgets(buff, buff_len, fp);
    buff[strcspn(buff, "\n")] = 0;

    pclose(fp);

    return TRUE;
}

static gboolean filter_host_info_header(const gchar *header, gchar *info, gchar *buff, gint buff_len) {
    gchar* head = strstr(info, header);

    if (head != NULL) {
        gint info_len = strlen(head + strlen(header));

        if (info_len > 1 && buff_len >= (info_len + 1)) {
            strcpy(buff, head + strlen(header));
        } else {
            PWL_LOG_ERR("host info buffer error, %d, %d, %s", info_len, buff_len, info);
        }
    }
}

void get_system_information() {
    memset(g_manufacturer, 0, PWL_MAX_MFR_SIZE);
    gchar manufacturer[INFO_BUFFER_SIZE];
    if (!get_host_info("dmidecode -t 1 | grep 'Manufacturer:'", manufacturer, INFO_BUFFER_SIZE)) {
        PWL_LOG_ERR("Get Manufacturer failed!");
    } else {
        if (!filter_host_info_header("Manufacturer: ", manufacturer, g_manufacturer, PWL_MAX_MFR_SIZE)) {
            PWL_LOG_ERR("Get Manufacturer info failed!");
        }
    }

    memset(g_skuid, 0, PWL_MAX_SKUID_SIZE);
    gchar skuid[INFO_BUFFER_SIZE];
    if (!get_host_info("dmidecode -t 1 | grep 'SKU Number:'", skuid, INFO_BUFFER_SIZE)) {
        PWL_LOG_ERR("Get SKU Number failed!");
    } else {
        if (!filter_host_info_header("SKU Number: ", skuid, g_skuid, PWL_MAX_SKUID_SIZE)) {
            PWL_LOG_ERR("Get SKU Number info failed!");
        }
    }
}

static gboolean signal_get_fw_version_handler(pwlCore *object, const gchar *arg, gpointer userdata) {
    if (NULL != g_signal_callback.callback_get_fw_version) {
        g_signal_callback.callback_get_fw_version(arg);
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

gboolean dbus_service_is_ready(void) {
    gchar *owner_name = NULL;
    owner_name = g_dbus_proxy_get_name_owner((GDBusProxy*)gp_proxy);
    if(NULL != owner_name) {
        PWL_LOG_DEBUG("Owner Name: %s", owner_name);
        g_free(owner_name);
        return true;
    } else {
        PWL_LOG_ERR("Owner Name is NULL.");
        return false;
    }
}

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
    uint32_t cid = PWL_CID_GET_FW_VER;
    send_message_queue(cid);
    send_message_queue(PWL_CID_GET_CRSM);
    return;
}

void get_carrier_from_sim(char *mcc, char *mnc)
{
    char command[128] = {0};
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

    FILE *file = fopen("common/mcc_mnc_list.csv", "r");
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
        strcpy(g_sim_carrier, PWL_UNKNOW_SIM_CARRIER);
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
    char mnc_len_temp;
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
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_OK,g_ap_version);
                else {
                    send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_ERROR, "");
                    PWL_LOG_ERR("AP version not ready!");
                }
                break;
            case PWL_CID_UPDATE_FW_VER:
                PWL_LOG_DEBUG("Receive update req, send req msg.");
                send_message_queue(PWL_CID_GET_FW_VER);
                break;
            case PWL_CID_GET_CRSM:
                if (strlen(message.response) > 0)
                {
                    mnc_len_temp = message.response[strlen(message.response)-2];
                    g_mnc_len = atoi(&mnc_len_temp);
                    send_message_queue(PWL_CID_GET_CIMI);
                }
                else
                {
                    g_mnc_len = 0;
                    strcpy(g_sim_carrier, PWL_UNKNOW_SIM_CARRIER);
                    if (1) PWL_LOG_DEBUG("Sim carrier: %s", g_sim_carrier);
                }
                break;
            case PWL_CID_GET_CIMI:
                PWL_LOG_DEBUG("%s", message.response);
                strncpy(sim_mcc_mnc[0], message.response, 3);
                strncpy(sim_mcc_mnc[1], message.response+3, g_mnc_len);
                get_carrier_from_sim(sim_mcc_mnc[0], sim_mcc_mnc[1]);
                if (1) PWL_LOG_DEBUG("Sim carrier: %s", g_sim_carrier);
                break;
            case PWL_CID_GET_SIM_CARRIER:
                send_message_reply(message.pwl_cid, PWL_MQ_ID_PREF, message.sender_id, PWL_CID_STATUS_OK, g_sim_carrier);
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
                break;
            default:
                PWL_LOG_ERR("Unknown pwl cid: %d", message.pwl_cid);
                break;
        }
    }

    return NULL;
}

void split_fw_versions(char *fw_version) {
    char *splitted_str;
    char *version_array[4];
    int i = 0;
    splitted_str = strtok(fw_version, "_");
    while (splitted_str != NULL)
    {
        version_array[i] = splitted_str;
        splitted_str = strtok(NULL, "_");
        i++;
    }
    g_main_fw_version = malloc(strlen(version_array[0]));
    g_ap_version = malloc(strlen(version_array[1]));
    g_carrier_version = malloc(strlen(version_array[2]));
    g_oem_version = malloc(strlen(version_array[3]));
    strcpy(g_main_fw_version, version_array[0]);
    strcpy(g_ap_version, version_array[1]);
    strcpy(g_carrier_version, version_array[2]);
    strcpy(g_oem_version, version_array[3]);

    PWL_LOG_DEBUG("main fw version: %s", g_main_fw_version);
    PWL_LOG_DEBUG("ap version: %s", g_ap_version);
    PWL_LOG_DEBUG("carrier version: %s", g_carrier_version);
    PWL_LOG_DEBUG("oem version: %s", g_oem_version);
}

gint main() {
    pwl_discard_old_messages(PWL_MQ_PATH_PREF);

    get_system_information();
    if (DEBUG) PWL_LOG_DEBUG("Manufacturer: %s", g_manufacturer);
    if (DEBUG) PWL_LOG_DEBUG("SKU Number: %s", g_skuid);

    signal_callback_t signal_callback;

    signal_callback.callback_get_fw_version = signal_callback_get_fw_version;

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
