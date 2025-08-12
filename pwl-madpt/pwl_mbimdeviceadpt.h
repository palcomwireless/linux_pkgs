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

#ifndef __PWL_MBIMDEVICEADPT_H__
#define __PWL_MBIMDEVICEADPT_H__

#include <glib.h>

#include "libmbim-glib.h"
#include "log.h"

typedef enum {
    PWL_MBIM_AT_COMMAND,
    PWL_MBIM_AT_TUNNEL
} madpt_mbim_intf_t;


typedef void (*mbim_device_ready_callback)(gboolean error);
typedef void (*mbim_at_resp_callback)(const gchar*);

gboolean pwl_mbimdeviceadpt_init(mbim_device_ready_callback cb);
void pwl_mbimdeviceadpt_deinit();
void pwl_mbimdeviceadpt_at_req(madpt_mbim_intf_t intf, gchar *command, mbim_at_resp_callback cb);
gboolean pwl_mbimdeviceadpt_port_wait();

__attribute__((weak)) gboolean mbim_message_intel_attunnel_at_command_response_parse (
    const MbimMessage *message,
	guint32 *out_command_resp_size,
    const guint8 **out_command_resp,
    GError **error);

__attribute__((weak)) MbimMessage *mbim_message_intel_attunnel_at_command_set_new(
    const guint32 command_req_size,
    const guint8 *command_req,
    GError **error);
#endif
