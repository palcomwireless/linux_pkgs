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

// PCI device hw reset
#define BOOTUP_CONFIG_FILE          "/opt/pwl/bootup_config"
#define CONFIG_MAX_BOOTUP_FAILURE   "MAX_BOOTUP_FAILURE"
#define DEVICE_MODE_NAME            "t7xx_mode"
#define DEVICE_REMOVE_NAME          "remove"
#define DEVICE_RESCAN_NAME          "rescan"

#define MODE_DEVICE_CAP             1
#define MODE_INTEL_REBOOT           2
#define MODE_INTEL_REBOOT_v2        3

#define DEVICE_HW_RESET             0
#define DEVICE_HW_RESCAN            1
#define DEVICE_CHECK_PERIOD         60
#define SHELL_CMD_RSP_LENGTH        128
#define DEVICE_MODE_LENGTH          16

#define DEVICE_REMOVE_DELAY         5   //Delay before remove device
#define DEVICE_RESCAN_DELAY         5   //Delay before rescan device
#define MAX_BOOTUP_FAILURE          3
#define MAX_RESCAN_FAILURE          3
#define TIMEOUT_SEC                 10

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
        {"0CDA", 717},
        {"0CF4", 883},
        {"0CF3", 883},
        {"0CE8", 883},
        {"0CF7", 883},
        {"0CF9", 595},
        {"0CFA", 595},
        {"0CF5", 883},
        {"0CF6", 595}
};

typedef void (*mbim_device_ready_callback)(void);

enum CHECK_MODULE_RETURNS {
    CHECK_PASS,
    CHECK_FAILURE,
    CHECK_IN_ABNORMAL_STATE
};

int gpio_init(void);
int set_gpio_status(int enable, int gpio);
static gboolean hw_reset();
// PCI device monitor and hw reset
gboolean pci_mbim_device_init(mbim_device_ready_callback cb);
gboolean find_mbim_port(gchar*, guint32);
gboolean find_abnormal_port(gchar*, guint32);
int do_shell_cmd(char *cmd, char *response);
int get_full_path(char *full_path);
int get_device_node_path(char *full_path, char *device_node_path);
int get_device_mode(char *device_node_path, char *mode);
int set_device_mode(char *device_node_path, char *node, char *value);
int do_pci_hw_reset(int reset_mode);
int check_module_info_v2();
static void pci_mbim_device_ready_cb();

#endif
