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

#ifndef __PWL_MBIMADPT_H__
#define __PWL_MBIMADPT_H__

#include "log.h"

#include <glib.h>

#define MBIM_MAX_RESP_SIZE          4096

#define MBIM_MSG_TYPE_QUERY         0
#define MBIM_MSG_TYPE_SET           1


typedef enum {
    MBIM_MSG_TYPE_INVALID    = 0x00000000,
    /* CONTROL MESSAGES SENT FROM THE HOST TO THE FUNCTION */
    MBIM_OPEN_MSG            = 0x00000001,
    MBIM_CLOSE_MSG           = 0x00000002,
    MBIM_COMMAND_MSG         = 0x00000003,
    MBIM_HOST_ERROR_MSG      = 0x00000004,
    /* CONTROL MESSAGE SENT FROM FUNCTION TO HOST */
    MBIM_OPEN_DONE           = 0x80000001,
    MBIM_CLOSE_DONE          = 0x80000002,
    MBIM_COMMAND_DONE        = 0x80000003,
    MBIM_FUNCTION_ERROR_MSG  = 0x80000004,
    MBIM_INDICATE_STATUS_MSG = 0x80000007
} mbim_msg_type_t;

typedef enum {
    MBIM_STATUS_SUCCESS                          = 0,
    MBIM_STATUS_BUSY                             = 1,
    MBIM_STATUS_FAILURE                          = 2,
    MBIM_STATUS_SIM_NOT_INSERTED                 = 3,
    MBIM_STATUS_BAD_SIM                          = 4,
    MBIM_STATUS_PIN_REQUIRED                     = 5,
    MBIM_STATUS_PIN_DISABLED                     = 6,
    MBIM_STATUS_NOT_REGISTERED                   = 7,
    MBIM_STATUS_PROVIDERS_NOT_FOUND              = 8,
    MBIM_STATUS_NO_DEVICE_SUPPORT                = 9,
    MBIM_STATUS_PROVIDER_NOT_VISIBLE             = 10,
    MBIM_STATUS_DATA_CLASS_NOT_AVAILABLE         = 11,
    MBIM_STATUS_PACKET_SERVICE_DETACHED          = 12,
    MBIM_STATUS_MAX_ACTIVATED_CONTEXTS           = 13,
    MBIM_STATUS_NOT_INITIALIZED                  = 14,
    MBIM_STATUS_VOICE_CALL_IN_PROGRESS           = 15,
    MBIM_STATUS_CONTEXT_NOT_ACTIVATED            = 16,
    MBIM_STATUS_SERVICE_NOT_ACTIVATED            = 17,
    MBIM_STATUS_INVALID_ACCESS_STRING            = 18,
    MBIM_STATUS_INVALID_USER_NAME_PWD            = 19,
    MBIM_STATUS_RADIO_POWER_OFF                  = 20,
    MBIM_STATUS_INVALID_PARAMETERS               = 21,
    MBIM_STATUS_READ_FAILURE                     = 22,
    MBIM_STATUS_WRITE_FAILURE                    = 23,
    /* 24 = Reserved */
    MBIM_STATUS_NO_PHONEBOOK                     = 25,
    MBIM_STATUS_PARAMETER_TOO_LONG               = 26,
    MBIM_STATUS_STK_BUSY                         = 27,
    MBIM_STATUS_OPERATION_NOT_ALLOWED            = 28,
    MBIM_STATUS_MEMORY_FAILURE                   = 29,
    MBIM_STATUS_INVALID_MEMORY_INDEX             = 30,
    MBIM_STATUS_MEMORY_FULL                      = 31,
    MBIM_STATUS_FILTER_NOT_SUPPORTED             = 32,
    MBIM_STATUS_DSS_INSTANCE_LIMIT               = 33,
    MBIM_STATUS_INVALID_DEVICE_SERVICE_OPERATION = 34,
    MBIM_STATUS_AUTH_INCORRECT_AUTN              = 35,
    MBIM_STATUS_AUTH_SYNC_FAILURE                = 36,
    MBIM_STATUS_AUTH_AMF_NOT_SET                 = 37,
    MBIM_STATUS_CONTEXT_NOT_SUPPORTED            = 38,
    MBIM_STATUS_SMS_UNKNOWN_SMSC_ADDRESS         = 100,
    MBIM_STATUS_SMS_NETWORK_TIMEOUT              = 101,
    MBIM_STATUS_SMS_LANG_NOT_SUPPORTED           = 102,
    MBIM_STATUS_SMS_ENCODING_NOT_SUPPORTED       = 103,
    MBIM_STATUS_SMS_FORMAT_NOT_SUPPORTED         = 104
} mbim_status_codes_t;

typedef enum {
    MBIM_ERROR_INVALID                  = 0,
    MBIM_ERROR_TIMEOUT_FRAGMENT         = 1,
    MBIM_ERROR_FRAGMENT_OUT_OF_SEQUENCE = 2,
    MBIM_ERROR_LENGTH_MISMATCH          = 3,
    MBIM_ERROR_DUPLICATED_TID           = 4,
    MBIM_ERROR_NOT_OPENED               = 5,
    MBIM_ERROR_UNKNOWN                  = 6,
    MBIM_ERROR_CANCEL                   = 7,
    MBIM_ERROR_MAX_TRANSFER             = 8
} mbim_protocol_error_codes_t;


/* MBIM MESSAGE HEADER */
typedef struct {
    mbim_msg_type_t message_type;
    guint32         message_length;
    guint32         transaction_id;
} mbim_message_header_t;

/* MBIM FRAGEMENT HEADER */
typedef struct {
    guint32         total_fragments;
    guint32         current_fragment;
} mbim_fragment_header_t;

/**
  Transmission structure of a UUID
  UUID transmission example: (a1a2a3a4-b1b2-c1c2-d1d2-e1e2e3e4e5e6)
**/
typedef struct {
    guint8          a[4];
    guint8          b[2];
    guint8          c[2];
    guint8          d[2];
    guint8          e[6];
} mbim_uuid_t;

static const mbim_uuid_t uuid_compal = {
    .a = { 0xa2, 0xa3, 0x2a, 0x97 },
    .b = { 0xca, 0xb1 },
    .c = { 0x4f, 0x57 },
    .d = { 0x9a, 0xe1 },
    .e = { 0x45, 0x1c, 0x74, 0xdd, 0xa9, 0x57 }
};

/* MBIM_OPEN_MSG */
typedef struct {
    mbim_message_header_t   message_header;
    guint32                 max_ctrl_transfer;
} mbim_open_message_t;

/* MBIM_CLOSE_MSG */
typedef struct {
    mbim_message_header_t   message_header;
} mbim_close_message_t;

typedef struct {
    gchar           at_cmd[256];
    guint32         at_cmd_len;
} mbim_at_cmd_t;

/* MBIM_COMMAND_MSG FORMAT */
typedef struct {
    mbim_message_header_t   message_header;
    mbim_fragment_header_t  fragment_header;
    mbim_uuid_t             device_service_id;
    guint32                 cid;
    guint32                 command_type;
    guint32                 information_buffer_length;
    guint8                  information_buffer[];
} mbim_command_msg_format_t;

struct mbim_done_t {
    mbim_message_header_t   message_header;
    guint32                 status;
};

/* MBIM_OPEN_DONE */
typedef struct mbim_done_t mbim_open_done_t;

/* MBIM_CLOSE_DONE */
typedef struct mbim_done_t mbim_close_done_t;

/* MBIM_COMMAND_DONE */
typedef struct {
    mbim_message_header_t   message_header;
    mbim_fragment_header_t  fragment_header;
    mbim_uuid_t             device_service_id;
    guint32                 cid;
    guint32                 status;
    guint32                 information_buffer_length;
    guint8                  information_buffer[];
} mbim_command_done_t;

typedef struct {
    void           *resp_data;
    guint32         data_buff;
    guint8          used;
} mbim_cmd_response_t;

typedef struct {
    gchar           at_cmd_rsp[MBIM_MAX_RESP_SIZE];
    guint32         at_cmd_rsp_len;
    gchar           at_cmd_req[256];
    guint32         at_cmd_req_len;
} mbim_at_cmd_resp_t;

typedef struct {
    mbim_message_header_t   message_header;
    guint32                 error_status_code;
} mbim_function_error_msg_t;

/* MBIM_INDICATE_STATUS_MSG FORMAT */
typedef struct {
    mbim_message_header_t   mbim_message_header;
    mbim_fragment_header_t  fragment_header;
    mbim_uuid_t             device_service_id;
    guint32                 cid;
    guint32                 information_buffer_length;
    guint8                  information_buffer[];
} mbim_indicate_status_msg_t;

gboolean pwl_mbimadpt_resp_parsing(const gchar *rsp, gchar *buff_ptr, guint32 buff_size);
gboolean pwl_mbimadpt_find_port(gchar *port_buff_ptr, guint32 port_buff_size);
gint pwl_mbimadpt_mbim_open();
gboolean pwl_mbimadpt_init(gint mbim_fd);
void pwl_mbimadpt_mbim_msg_signal();
void pwl_mbimadpt_mbim_close(gint *fd);
gboolean pwl_mbimadpt_deinit_async();
void pwl_mbimadpt_deinit();
void pwl_mbimadpt_at_req(gchar *command);
gint response_msg_parsing(gchar *read_buff_ptr, guint32 read_buff_size);
void parse_command_done(gchar *read_buff_ptr, guint32 read_buff_size);

#endif
