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


typedef void (*signal_get_fw_version_callback)(const gchar*);

typedef struct {
    signal_get_fw_version_callback callback_get_fw_version;
} signal_callback_t;

void split_fw_versions(char *fw_version);

#endif
