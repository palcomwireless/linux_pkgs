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

#ifndef __PWL_CORE_H__
#define __PWL_CORE_H__

#include "CoreGdbusGenerated.h"
#include "libmbim-glib.h"

typedef struct {
    char *skuid;
    int gpio;
} s_skuid_to_gpio;

s_skuid_to_gpio g_skuid_to_gpio[] = {
        {"0CBD", 883},
        {"0CC2", 883},
        {"0CC1", 883},
        {"0CC4", 883},
        {"0CB5", 717},
        {"0CB6", 717},
        {"0CB7", 595},
        {"0CB8", 595},
        {"0CB2", 717},
        {"0CB3", 717},
        {"0CB4", 595},
        {"0CB9", 717},
        {"0CBA", 717},
        {"0CBB", 717},
        {"0CBC", 595},
        {"0CD9", 717},
        {"0CDA", 717}
};

int gpio_init(void);
int set_gpio_status(int enable, int gpio);
static gboolean hw_reset();

#endif
