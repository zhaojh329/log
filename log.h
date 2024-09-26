/* SPDX-License-Identifier: MIT */
/*
 * Author: Jianhui Zhao <zhaojh329@gmail.com>
 */

#ifndef __LOG_H
#define __LOG_H

#include <stdbool.h>
#include <syslog.h>
#include <string.h>

enum {
    LOG_FLAG_LF   = 1 << 0, /* append a character '\n' to every log message */
    LOG_FLAG_FILE = 1 << 1, /* filename and line number */
    LOG_FLAG_PATH = 1 << 2  /* full file path and line number */
};

extern int __log_level__;
extern int __log_flags__;

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

/* This is useful. The code in the parameter is not executed when the log level is lower than the set value */
#define log_conditional(priority, fmt...)                          \
    do {                                               \
        int pri = LOG_PRI(priority);                   \
                                                       \
        if (pri <= __log_level__)                      \
            ___log(__FILENAME__, __LINE__, pri, fmt);  \
    } while (0)

#define log_debug(fmt...)     log_conditional(LOG_DEBUG, fmt)
#define log_info(fmt...)      log_conditional(LOG_INFO, fmt)
#define log_warn(fmt...)      log_conditional(LOG_WARNING, fmt)
#define log_err(fmt...)       log_conditional(LOG_ERR, fmt)

__attribute__((format(printf, 4, 5)))
void ___log(const char *filename, int line, int priority, const char *fmt, ...);

static inline void set_log_level(int level)
{
    __log_level__ = level;
}

void set_log_ident(const char *val);
void set_log_path(const char *path);
void set_log_flags(int flags);

#endif
