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

#ifndef __PWL_MADPT_H__
#define __PWL_MADPT_H__

#include "CoreGdbusGenerated.h"

#define MADPT_FORCE_MBIMCLI    0

#define RET_SIGNAL_HANDLE_SIZE 3
#define PWL_MBIM_OPEN_WAIT_MAX 3
#define PWL_MBIM_ERR_MAX       2

#define OEM_PRI_UPDATE_START     0
#define OEM_PPI_UPDATE_INIT      1   // waiting for modem initialize the RF NV item
#define OEM_PRI_UPDATE_RESET     2   // Reset module
#define OEM_PRI_UPDATE_NORESET   3   // Nothing

#define OEM_PRI_RESET_NOT_READY         0  // cont. to query reset state
#define OEM_PRI_RESET_UPDATE_SUCCESS    1  // trigger modem reboot
#define OEM_PRI_RESET_UPDATE_FAILED     2  // error
#define OEM_PRI_RESET_NO_NEED_UPDATE    9  // trigger modem reboot

gboolean at_resp_parsing(const gchar *rsp, gchar *buff_ptr, guint32 buff_size);
void jp_fcc_config();
void restart();
void mbim_error_check();

#endif
