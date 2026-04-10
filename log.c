/* SPDX-License-Identifier: MIT */
/*
 * Author: Jianhui Zhao <zhaojh329@gmail.com>
 */

#include <linux/limits.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <libgen.h>
#include <limits.h>
#include <errno.h>

#include "log.h"

#define LOG_ROLL_DEFAULT_SIZE (100 * 1024)
#define LOG_ROLL_DEFAULT_COUNT 10

int __log_level__ = LOG_INFO;
int __log_flags__ = LOG_FLAG_LF;

static const char *log_path;
static const char *log_base;
static const char *log_dir;

static char ident[32];

static size_t log_roll_size = LOG_ROLL_DEFAULT_SIZE;
static int log_roll_count = LOG_ROLL_DEFAULT_COUNT;

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

struct rolled_file {
    char *path;
    unsigned int seq;
};

static int cmp_rolled_file_desc(const void *a, const void *b)
{
    const struct rolled_file *fa = a;
    const struct rolled_file *fb = b;

    if (fa->seq > fb->seq)
        return -1;

    if (fa->seq < fb->seq)
        return 1;

    return strcmp(fb->path, fa->path);
}

static bool parse_rolled_seq(const char *name, size_t base_len, unsigned int *seq)
{
    const char *p;
    char *end;
    unsigned long n;

    if (strncmp(name, log_base, base_len))
        return false;

    if (name[base_len] != '.')
        return false;

    p = name + base_len + 1;
    if (!isdigit((unsigned char)*p))
        return false;

    errno = 0;
    n = strtoul(p, &end, 10);
    if (errno || n > UINT_MAX)
        return false;

    if (end == p || *end != '.' || !end[1])
        return false;

    *seq = (unsigned int)n;

    return true;
}

static unsigned int find_next_roll_seq(void)
{
    unsigned int max_seq = 0;
    struct dirent *entry;
    DIR *dp;

    dp = opendir(log_dir);
    if (!dp)
        return 0;

    while ((entry = readdir(dp)) != NULL) {
        unsigned int seq;

        if (!parse_rolled_seq(entry->d_name, strlen(log_base), &seq))
            continue;

        if (seq > max_seq)
            max_seq = seq;
    }

    closedir(dp);

    if (max_seq == UINT_MAX)
        return UINT_MAX;

    return max_seq + 1;
}

static bool should_roll()
{
    struct stat st;

    if (log_roll_size == 0)
        return false;

    if (stat(log_path, &st) < 0)
        return false;

    return st.st_size >= log_roll_size;
}

static void cleanup_rolled_files()
{
    struct rolled_file *files = NULL;
    struct dirent *entry;
    DIR *dp;
    int count = 0;

    if (log_roll_count == 0)
        return;

    dp = opendir(log_dir);
    if (!dp)
        return;

    while ((entry = readdir(dp)) != NULL) {
        struct rolled_file *tmp;
        char fullpath[PATH_MAX];
        struct stat st;
        unsigned int seq;

        if (!parse_rolled_seq(entry->d_name, strlen(log_base), &seq))
            continue;

        if (snprintf(fullpath, sizeof(fullpath), "%s/%s", log_dir, entry->d_name) >= sizeof(fullpath))
            continue;

        if (stat(fullpath, &st) < 0 || !S_ISREG(st.st_mode))
            continue;

        tmp = realloc(files, sizeof(*files) * (count + 1));
        if (!tmp)
            break;
        files = tmp;

        files[count].path = strdup(fullpath);
        if (!files[count].path)
            break;
        files[count].seq = seq;
        count++;
    }

    closedir(dp);

    if (count > log_roll_count) {
        int i;

        qsort(files, count, sizeof(*files), cmp_rolled_file_desc);

        for (i = log_roll_count; i < count; i++)
            unlink(files[i].path);
    }

    if (files) {
        int i;

        for (i = 0; i < count; i++)
            free(files[i].path);

        free(files);
    }
}

static void rotate_log(void)
{
    char rolled_path[PATH_MAX];
    char seq_buf[16];
    char ts[32];
    unsigned int seq;
    struct tm tm;
    time_t now;

    now = time(NULL);
    localtime_r(&now, &tm);
    strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tm);

    seq = find_next_roll_seq();
    if (seq == UINT_MAX)
        return;

    if (seq < 10000)
        snprintf(seq_buf, sizeof(seq_buf), "%04u", seq);
    else
        snprintf(seq_buf, sizeof(seq_buf), "%u", seq);

    if (snprintf(rolled_path, sizeof(rolled_path), "%s.%s.%s", log_path, seq_buf, ts) >= sizeof(rolled_path))
        return;

    if (rename(log_path, rolled_path))
        return;

    cleanup_rolled_files();
}

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

    if (should_roll())
        rotate_log();

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
    char *dirc = NULL;

    priority = LOG_PRI(priority);

    if (priority > __log_level__)
        return;

    if (__log_flags__ & LOG_FLAG_FILE || __log_flags__ & LOG_FLAG_PATH) {
        if (!(__log_flags__ & LOG_FLAG_PATH)) {
            dirc = strdup(filename);
            filename = basename(dirc);
        }
        snprintf(new_fmt, sizeof(new_fmt), "(%s:%3d) %s", filename, line, fmt);
        if (!(__log_flags__ & LOG_FLAG_PATH))
            free(dirc);
    } else {
        snprintf(new_fmt, sizeof(new_fmt), "%s", fmt);
    }

    va_start(ap, fmt);
    log_write(priority, new_fmt, ap);
    va_end(ap);
}

void set_log_ident(const char *val)
{
    if (!val)
        val = "";

    snprintf(ident, sizeof(ident), "%s", val);

    if (isatty(STDOUT_FILENO))
        return;

    closelog();
    openlog(ident, LOG_PID, LOG_DAEMON);
}

static inline void log_to_stdout(int priority, const char *fmt, va_list ap)
{
    __log_to_file(stdout, priority, fmt, ap);
}

static inline void log_to_syslog(int priority, const char *fmt, va_list ap)
{
    vsyslog(priority, fmt, ap);
}

static void init_log_write()
{
    if (isatty(STDOUT_FILENO)) {
        log_write = log_to_stdout;
    } else {
        log_write = log_to_syslog;

        openlog(ident, LOG_PID, LOG_DAEMON);
    }
}

int set_log_path(const char *path)
{
    char tmp_path[PATH_MAX];

    if (log_path) {
        free((void *)log_path);
        log_path = NULL;

        free((void *)log_dir);
        log_dir = NULL;
    }

    if (path && path[0]) {
        size_t len = strlen(path);
        char *slash;

        if (len >= sizeof(tmp_path))
            return -1;

        if (strstr(path, "..") ||
            strstr(path, "./") ||
            strstr(path, "/.") ||
            strstr(path, "//"))
            return -1;

        if (path[len - 1] == '/')
            return -1;

        strcpy(tmp_path, path);

        log_path = strdup(tmp_path);
        if (!log_path)
            return -1;

        slash = strrchr(log_path, '/');
        if (!slash) {
            log_base = log_path;
            log_dir = strdup(".");
        } else {
            log_base = slash + 1;

            if (slash == log_path) {
                log_dir = strdup("/");
            } else {
                log_dir = strndup(log_path, slash - log_path);
                if (!log_dir) {
                    free((void *)log_path);
                    log_path = NULL;
                    return -1;
                }
            }
        }
    } else {
        log_path = NULL;
        log_dir = NULL;
    }

    if (!log_path) {
        init_log_write();
        return 0;
    }

    if (access(log_dir, W_OK) < 0) {
        free((void *)log_path);
        log_path = NULL;

        free((void *)log_dir);
        log_dir = NULL;

        return -1;
    }

    log_write = log_to_file;

    if (!isatty(STDOUT_FILENO))
        closelog();

    return 0;
}

void set_log_flags(int flags)
{
    __log_flags__ = flags;
}

void set_log_roll_size(size_t size)
{
    log_roll_size = size;
}

void set_log_roll_count(int count)
{
    log_roll_count = count > 0 ? count : LOG_ROLL_DEFAULT_COUNT;
}

static void __attribute__((constructor)) init()
{
    static char line[64];
    FILE *self;
    const char *p = "";
    char *sbuf;

    if ((self = fopen("/proc/self/status", "r")) != NULL) {
        while (fgets(line, sizeof(line), self)) {
            if (!strncmp(line, "Name:", 5)) {
                strtok_r(line, "\t\n", &sbuf);
                {
                    char *name = strtok_r(NULL, "\t\n", &sbuf);
                    if (name && name[0])
                        p = name;
                }
                break;
            }
        }
        fclose(self);
    }

    snprintf(ident, sizeof(ident), "%s", p);

    init_log_write();
}
