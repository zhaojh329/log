/* SPDX-License-Identifier: MIT */
/*
 * Author: Jianhui Zhao <zhaojh329@gmail.com>
 */

#ifndef __LOG_H
#define __LOG_H

#include <stdbool.h>
#include <syslog.h>
#include <string.h>

extern int __log_level__;

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

/* This is useful. The code in the parameter is not executed when the log level is lower than the set value */
#define log(priority, fmt...)                          \
    do {                                               \
        int pri = LOG_PRI(priority);                   \
                                                       \
        if (pri <= __log_level__)                      \
            ___log(__FILENAME__, __LINE__, pri, fmt);  \
    } while (0)

#define log_debug(fmt...)     log(LOG_DEBUG, fmt)
#define log_info(fmt...)      log(LOG_INFO, fmt)
#define log_warn(fmt...)      log(LOG_WARNING, fmt)
#define log_err(fmt...)       log(LOG_ERR, fmt)

__attribute__((format(printf, 4, 5)))
void ___log(const char *filename, int line, int priority, const char *fmt, ...);

static inline void set_log_level(int level)
{
    __log_level__ = level;
}

void set_log_path(const char *path);
void set_log_newline(bool val);

#endif
