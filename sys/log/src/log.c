/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "sysinit/sysinit.h"
#include "syscfg/syscfg.h"
#include "os/os.h"
#include "cbmem/cbmem.h"
#include "log/log.h"

#if MYNEWT_VAL(LOG_CLI)
#include "shell/shell.h"
#endif

struct log_info g_log_info;

static STAILQ_HEAD(, log) g_log_list = STAILQ_HEAD_INITIALIZER(g_log_list);
static uint8_t log_inited;

#if MYNEWT_VAL(LOG_CLI)
int shell_log_dump_all_cmd(int, char **);
struct shell_cmd g_shell_log_cmd = {
    .sc_cmd = "log",
    .sc_cmd_func = shell_log_dump_all_cmd
};
#endif

void
log_init(void)
{
    int rc;

    (void)rc;

    if (log_inited) {
        return;
    }
    log_inited = 1;

    g_log_info.li_version = LOG_VERSION_V2;
    g_log_info.li_index = 0;
    g_log_info.li_timestamp = 0;

#if MYNEWT_VAL(LOG_CLI)
    shell_cmd_register(&g_shell_log_cmd);
#endif

#if MYNEWT_VAL(LOG_NEWTMGR)
    rc = log_nmgr_register_group();
    SYSINIT_PANIC_ASSERT(rc == 0);
#endif
}

struct log *
log_list_get_next(struct log *log)
{
    struct log *next;

    if (log == NULL) {
        next = STAILQ_FIRST(&g_log_list);
    } else {
        next = STAILQ_NEXT(log, l_next);
    }

    return (next);
}

/**
 * Indicates whether the specified log has been regiestered.
 */
static int
log_registered(struct log *log)
{
    struct log *cur;

    STAILQ_FOREACH(cur, &g_log_list, l_next) {
        if (cur == log) {
            return 1;
        }
    }

    return 0;
}

/*
 * Associate an instantiation of a log with the logging infrastructure
 */
int
log_register(char *name, struct log *log, const struct log_handler *lh,
             void *arg, uint8_t level)
{
    log->l_name = name;
    log->l_log = (struct log_handler *)lh;
    log->l_arg = arg;
    log->l_level = level;

    /*assert(!log_registered(log));*/
    if (!log_registered(log)) {
        STAILQ_INSERT_TAIL(&g_log_list, log, l_next);
    }

    return (0);
}

int
log_append(struct log *log, uint16_t module, uint16_t level, void *data,
        uint16_t len)
{
    struct log_entry_hdr *ue;
    int rc;
    struct os_timeval tv;

    if (log->l_name == NULL || log->l_log == NULL) {
        rc = -1;
        goto err;
    }

    /*
     * If the log message is below what this log instance is
     * configured to accept, then just drop it.
     */
    if (level < log->l_level) {
        rc = -1;
        goto err;
    }

    ue = (struct log_entry_hdr *) data;

    /* Could check for li_index wraparound here */
    g_log_info.li_index++;

    /* Try to get UTC Time */
    rc = os_gettimeofday(&tv, NULL);
    if (rc || tv.tv_sec < UTC01_01_2016) {
        ue->ue_ts = os_get_uptime_usec();
    } else {
        ue->ue_ts = tv.tv_sec * 1000000 + tv.tv_usec;
    }

    g_log_info.li_timestamp = ue->ue_ts;
    ue->ue_level = level;
    ue->ue_module = module;
    ue->ue_index = g_log_info.li_index;

    rc = log->l_log->log_append(log, data, len + LOG_ENTRY_HDR_SIZE);
    if (rc != 0) {
        goto err;
    }

    return (0);
err:
    return (rc);
}

void
log_printf(struct log *log, uint16_t module, uint16_t level, char *msg, ...)
{
    va_list args;
    char buf[LOG_ENTRY_HDR_SIZE + LOG_PRINTF_MAX_ENTRY_LEN];
    int len;

    va_start(args, msg);
    len = vsnprintf(&buf[LOG_ENTRY_HDR_SIZE], LOG_PRINTF_MAX_ENTRY_LEN, msg,
            args);
    if (len >= LOG_PRINTF_MAX_ENTRY_LEN) {
        len = LOG_PRINTF_MAX_ENTRY_LEN-1;
    }

    log_append(log, module, level, (uint8_t *) buf, len);
}

int
log_walk(struct log *log, log_walk_func_t walk_func, void *arg)
{
    int rc;

    rc = log->l_log->log_walk(log, walk_func, arg);
    if (rc != 0) {
        goto err;
    }

    return (0);
err:
    return (rc);
}

int
log_read(struct log *log, void *dptr, void *buf, uint16_t off,
        uint16_t len)
{
    int rc;

    rc = log->l_log->log_read(log, dptr, buf, off, len);

    return (rc);
}

int
log_flush(struct log *log)
{
    int rc;

    rc = log->l_log->log_flush(log);
    if (rc != 0) {
        goto err;
    }

    g_log_info.li_index = 0;

    return (0);
err:
    return (rc);
}
