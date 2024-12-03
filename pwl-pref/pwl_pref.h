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

#ifndef __PWL_PREF_H__
#define __PWL_PREF_H__

#include "CoreGdbusGenerated.h"

#define RET_SIGNAL_HANDLE_SIZE 4
#define PWL_PREF_CMD_RETRY_LIMIT 12
#define PWL_PREF_GET_SIM_INFO_DELAY 5
#define PWL_PREF_SET_CARRIER_RETRY_LIMIT 3
#define MAX_PATH  260
#define MAX_PCIE_VERSION_LENGTH         512
#define MAX_PCIE_AP_VERSION_LENGTH      10

typedef void (*signal_get_fw_version_callback)(const gchar*);
typedef void (*signal_get_sub_state_change_callback)(gint arg_status);

typedef struct {
    signal_get_fw_version_callback callback_get_fw_version;
    signal_get_sub_state_change_callback callback_sim_state_change;
} signal_callback_t;

void split_fw_versions(char *fw_version);
gint get_sim_carrier_info(int retry_delay, int retry_limit);
gint get_preferred_carrier();
gint set_preferred_carrier(char *carrier, int retry_limit);
gint get_preferred_carrier_id();
gint set_preferred_carrier_id(int carrier_id);
gboolean is_need_cxp_reboot(int current_carrier_id, int pref_carrier_id);
gboolean is_cxp_carrier(int carrier_id);
void split_pcie_device_versions(char *sw_version);
#endif
