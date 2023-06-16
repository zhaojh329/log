/* SPDX-License-Identifier: MIT */
/*
 * Author: Jianhui Zhao <zhaojh329@gmail.com>
 */

#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "log.h"

int __log_level__ = LOG_INFO;
int __log_flags__ = LOG_FLAG_LF;

static const char *ident;
static const char *log_path;

static void (*log_write)(int priority, const char *fmt, va_list ap);

static const char *prioritynames[] = {
    [LOG_EMERG] = "emerg",
    [LOG_ALERT] = "alert",
    [LOG_CRIT] = "crit",
    [LOG_ERR] = "err",
    [LOG_WARNING] = "warn",
    [LOG_NOTICE] = "notice",
    [LOG_INFO] = "info",
    [LOG_DEBUG] = "debug"
};

static void __log_to_file(FILE *fp, int priority, const char *fmt, va_list ap)
{
    time_t now;
    struct tm tm;
    char buf[32];

    now = time(NULL);
    localtime_r(&now, &tm);
    strftime(buf, sizeof(buf), "%Y/%m/%d %H:%M:%S", &tm);

    fprintf(fp, "%s %-5s %s[%d]: ", buf, prioritynames[priority], ident, getpid());
    vfprintf(fp, fmt, ap);

    if (__log_flags__ & LOG_FLAG_LF)
        fputc('\n', fp);
}

static inline void log_to_file(int priority, const char *fmt, va_list ap)
{
    FILE *fp;

    fp = fopen(log_path, "a");
    if (!fp)
        return;

    __log_to_file(fp, priority, fmt, ap);

    fclose(fp);
}

void ___log(const char *filename, int line, int priority, const char *fmt, ...)
{
    char new_fmt[256];
    va_list ap;

    priority = LOG_PRI(priority);

    if (priority > __log_level__)
        return;

    if (__log_flags__ & LOG_FLAG_FILE || __log_flags__ & LOG_FLAG_PATH) {
        if (!(__log_flags__ & LOG_FLAG_PATH))
            filename = basename(filename);
        snprintf(new_fmt, sizeof(new_fmt), "(%s:%3d) %s", filename, line, fmt);
    } else {
        snprintf(new_fmt, sizeof(new_fmt), "%s", fmt);
    }

    va_start(ap, fmt);
    log_write(priority, new_fmt, ap);
    va_end(ap);
}

void set_log_path(const char *path)
{
    log_path = path;

    if (log_path) {
        log_write = log_to_file;
        closelog();
    }
}

void set_log_flags(int flags)
{
    __log_flags__ = flags;
}

static inline void log_to_stdout(int priority, const char *fmt, va_list ap)
{
    __log_to_file(stdout, priority, fmt, ap);
}

static inline void log_to_syslog(int priority, const char *fmt, va_list ap)
{
    vsyslog(priority, fmt, ap);
}

static void __attribute__((constructor)) init()
{
    static char line[64];
    FILE *self;
    char *p = NULL;
    char *sbuf;

    if ((self = fopen("/proc/self/status", "r")) != NULL) {
        while (fgets(line, sizeof(line), self)) {
            if (!strncmp(line, "Name:", 5)) {
                strtok_r(line, "\t\n", &sbuf);
                p = strtok_r(NULL, "\t\n", &sbuf);
                break;
            }
        }
        fclose(self);
    }

    ident = p;

    if (isatty(STDOUT_FILENO)) {
        log_write = log_to_stdout;
    } else {
        log_write = log_to_syslog;

        openlog(ident, 0, LOG_DAEMON | LOG_PID);
    }
}
