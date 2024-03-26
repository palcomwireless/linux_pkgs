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

#ifndef __LOG_H__
#define __LOG_H__

#include <syslog.h>

#define DEBUG                       0
#define TIME_LOG                    0

#if SYS_LOG
#define PWL_LOG_DEBUG(format, ...)      syslog(LOG_DEBUG, format, ## __VA_ARGS__)
#define PWL_LOG_INFO(format, ...)       syslog(LOG_INFO, format, ## __VA_ARGS__)
#define PWL_LOG_ERR(format, ...)        syslog(LOG_ERR, format, ## __VA_ARGS__)
#else
#if (TIME_LOG)
#include <sys/time.h>
#define PWL_LOG_PRINT(format, ...) \
    do { \
        struct timeval tv; \
        gettimeofday(&tv, NULL); \
        struct tm *local_time = localtime(&tv.tv_sec); \
        g_print("%04d-%02d-%02d %02d:%02d:%02d.%03ld ", \
            local_time->tm_year + 1900, local_time->tm_mon + 1, local_time->tm_mday, \
            local_time->tm_hour, local_time->tm_min, local_time->tm_sec, tv.tv_usec / 1000); \
        g_print(format "\n", ## __VA_ARGS__); \
    } while(0)

#define PWL_LOG_DEBUG(format, ...)      PWL_LOG_PRINT(format, ## __VA_ARGS__)
#define PWL_LOG_INFO(format, ...)       PWL_LOG_PRINT(format, ## __VA_ARGS__)
#define PWL_LOG_ERR(format, ...)        PWL_LOG_PRINT(format, ## __VA_ARGS__)
#else
#define PWL_LOG_DEBUG(format, ...)      g_print(format "\n", ## __VA_ARGS__)
#define PWL_LOG_INFO(format, ...)       g_print(format "\n", ## __VA_ARGS__)
#define PWL_LOG_ERR(format, ...)        g_print(format "\n", ## __VA_ARGS__)
#endif
#endif

#endif
