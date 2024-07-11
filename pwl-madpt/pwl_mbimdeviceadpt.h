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

typedef void (*mbim_device_ready_callback)(gboolean error);
typedef void (*mbim_at_resp_callback)(const gchar*);

gboolean pwl_mbimdeviceadpt_init(mbim_device_ready_callback cb);
void pwl_mbimdeviceadpt_deinit();
void pwl_mbimdeviceadpt_at_req(gchar *command, mbim_at_resp_callback cb);
gboolean pwl_mbimdeviceadpt_port_wait();

#endif
