/**
 * @file netconf_monitoring.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief ietf-netconf-monitoring statistics and counters
 *
 * Copyright (c) 2019 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include "netconf_monitoring.h"

#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <libyang/libyang.h>
#include <nc_server.h>

#include "common.h"
#include "log.h"

#define NCM_TIMEZONE "CET"

struct ncm stats;

void
ncm_init(void)
{
    stats.netconf_start_time = time(NULL);
    pthread_mutex_init(&stats.lock, NULL);
}

void
ncm_destroy(void)
{
    free(stats.sessions);
    free(stats.session_stats);
    pthread_mutex_destroy(&stats.lock);
}

static uint32_t
find_session_idx(struct nc_session *session)
{
    uint32_t i;

    for (i = 0; i < stats.session_count; ++i) {
        if (nc_session_get_id(stats.sessions[i]) == nc_session_get_id(session)) {
            return i;
        }
    }

    EINT;
    return 0;
}

static int
ncm_is_monitored(struct nc_session *session)
{
    switch (nc_session_get_ti(session)) {
#ifdef NC_ENABLED_SSH
    case NC_TI_LIBSSH:
#endif
#ifdef NC_ENABLED_TLS
    case NC_TI_OPENSSL:
#endif
#if defined(NC_ENABLED_SSH) || defined(NC_ENABLED_TLS)
        return 1;
#endif
    default:
        break;
    }

    return 0;
}

void
ncm_session_rpc(struct nc_session *session)
{
    if (!ncm_is_monitored(session)) {
        return;
    }

    pthread_mutex_lock(&stats.lock);

    ++stats.session_stats[find_session_idx(session)].in_rpcs;
    ++stats.global_stats.in_rpcs;

    pthread_mutex_unlock(&stats.lock);
}

void
ncm_session_bad_rpc(struct nc_session *session)
{
    if (!ncm_is_monitored(session)) {
        return;
    }

    pthread_mutex_lock(&stats.lock);

    ++stats.session_stats[find_session_idx(session)].in_bad_rpcs;
    ++stats.global_stats.in_bad_rpcs;

    pthread_mutex_unlock(&stats.lock);
}

void
ncm_session_rpc_reply_error(struct nc_session *session)
{
    if (!ncm_is_monitored(session)) {
        return;
    }

    pthread_mutex_lock(&stats.lock);

    ++stats.session_stats[find_session_idx(session)].out_rpc_errors;
    ++stats.global_stats.out_rpc_errors;

    pthread_mutex_unlock(&stats.lock);
}

void
ncm_session_notification(struct nc_session *session)
{
    if (!ncm_is_monitored(session)) {
        return;
    }

    pthread_mutex_lock(&stats.lock);

    ++stats.session_stats[find_session_idx(session)].out_notifications;
    ++stats.global_stats.out_notifications;

    pthread_mutex_unlock(&stats.lock);
}

void
ncm_session_add(struct nc_session *session)
{
    void *new;

    if (!ncm_is_monitored(session)) {
        WRN("Session %d uses a transport protocol not supported by ietf-netconf-monitoring, will not be monitored.",
                nc_session_get_id(session));
        return;
    }

    pthread_mutex_lock(&stats.lock);

    ++stats.in_sessions;

    ++stats.session_count;
    new = realloc(stats.sessions, stats.session_count * sizeof *stats.sessions);
    if (!new) {
        EMEM;
        return;
    }
    stats.sessions = new;
    new = realloc(stats.session_stats, stats.session_count * sizeof *stats.session_stats);
    if (!new) {
        EMEM;
        return;
    }
    stats.session_stats = new;

    stats.sessions[stats.session_count - 1] = session;
    memset(&stats.session_stats[stats.session_count - 1], 0, sizeof *stats.session_stats);

    pthread_mutex_unlock(&stats.lock);
}

void
ncm_session_del(struct nc_session *session)
{
    uint32_t i;

    if (!ncm_is_monitored(session)) {
        return;
    }

    pthread_mutex_lock(&stats.lock);

    if (!nc_session_get_term_reason(session)) {
        EINT;
    }

    if (nc_session_get_term_reason(session) != NC_SESSION_TERM_CLOSED) {
        ++stats.dropped_sessions;
    }

    i = find_session_idx(session);
    --stats.session_count;
    if (stats.session_count && (i < stats.session_count)) {
        memmove(&stats.sessions[i], &stats.sessions[i + 1], (stats.session_count - i) * sizeof *stats.sessions);
        memmove(&stats.session_stats[i], &stats.session_stats[i + 1], (stats.session_count - i) * sizeof *stats.session_stats);
    }

    pthread_mutex_unlock(&stats.lock);
}

void
ncm_bad_hello(struct nc_session *session)
{
    if (!ncm_is_monitored(session)) {
        return;
    }

    pthread_mutex_lock(&stats.lock);

    ++stats.in_bad_hellos;

    pthread_mutex_unlock(&stats.lock);
}

uint32_t
ncm_session_get_notification(struct nc_session *session)
{
    uint32_t count;

    if (!ncm_is_monitored(session)) {
        return 0;
    }

    pthread_mutex_lock(&stats.lock);

    count = stats.session_stats[find_session_idx(session)].out_notifications;

    pthread_mutex_unlock(&stats.lock);

    return count;
}

static void
ncm_data_add_ds_lock(sr_conn_ctx_t *conn, const char *ds_str, sr_datastore_t ds, struct lyd_node *parent)
{
    struct lyd_node *list, *cont, *cont2;
    char buf[26];
    int rc, is_locked;
    uint32_t nc_id;
    time_t ts;

    lyd_new_list(parent, NULL, "datastore", 0, &list, ds_str);
    rc = sr_get_lock(conn, ds, NULL, &is_locked, NULL, &nc_id, &ts);
    if (rc != SR_ERR_OK) {
        WRN("Failed to learn about %s lock (%s).", ds_str, sr_strerror(rc));
    } else if (is_locked) {
        lyd_new_inner(list, NULL, "locks", 0, &cont);
        lyd_new_inner(cont, NULL, "global-lock", 0, &cont2);
        sprintf(buf, "%u", nc_id);
        lyd_new_term(cont2, NULL, "locked-by-session", buf, 0, NULL);
        nc_time2datetime(ts, NCM_TIMEZONE, buf);
        lyd_new_term(cont2, NULL, "locked-time", buf, 0, NULL);
    }
}

int
np2srv_ncm_oper_cb(sr_session_ctx_t *session, uint32_t UNUSED(sub_id), const char *UNUSED(module_name),
        const char *UNUSED(path), const char *UNUSED(request_xpath), uint32_t UNUSED(request_id),
        struct lyd_node **parent, void *UNUSED(private_data))
{
    struct lyd_node *root = NULL, *cont, *list;
    const struct lys_module *mod;
    sr_conn_ctx_t *conn;
    struct ly_ctx *ly_ctx;
    const char **cpblts;
    char buf[26];
    uint32_t i;

    conn = sr_session_get_connection(session);
    ly_ctx = (struct ly_ctx *)sr_get_context(conn);

    if (lyd_new_path(NULL, ly_ctx, "/ietf-netconf-monitoring:netconf-state", NULL, 0, &root)) {
        goto error;
    }

    /* capabilities */
    lyd_new_inner(root, NULL, "capabilities", 0, &cont);

    cpblts = nc_server_get_cpblts_version(ly_ctx, LYS_VERSION_1_0);
    if (!cpblts) {
        goto error;
    }

    for (i = 0; cpblts[i]; ++i) {
        lyd_new_term(cont, NULL, "capability", cpblts[i], 0, NULL);
        lydict_remove(ly_ctx, cpblts[i]);
    }
    free(cpblts);

    /* datastore locks */
    lyd_new_inner(root, NULL, "datastores", 0, &cont);
    ncm_data_add_ds_lock(conn, "running", SR_DS_RUNNING, cont);
    ncm_data_add_ds_lock(conn, "startup", SR_DS_STARTUP, cont);
    ncm_data_add_ds_lock(conn, "candidate", SR_DS_CANDIDATE, cont);

    /* schemas */
    lyd_new_inner(root, NULL, "schemas", 0, &cont);

    i = 0;
    while ((mod = ly_ctx_get_module_iter(ly_ctx, &i))) {
        lyd_new_list(cont, NULL, "schema", 0, &list, mod->name, mod->revision ? mod->revision : "", "yang");
        lyd_new_term(list, NULL, "namespace", mod->ns, 0, NULL);
        lyd_new_term(list, NULL, "location", "NETCONF", 0, NULL);

        lyd_new_list(cont, NULL, "schema", 0, &list, mod->name, mod->revision ? mod->revision : "", "yin");
        lyd_new_term(list, NULL, "namespace", mod->ns, 0, NULL);
        lyd_new_term(list, NULL, "location", "NETCONF", 0, NULL);
    }

    /* sessions */
    pthread_mutex_lock(&stats.lock);

    if (stats.session_count) {
        lyd_new_inner(root, NULL, "sessions", 0, &cont);

        for (i = 0; i < stats.session_count; ++i) {
            sprintf(buf, "%u", nc_session_get_id(stats.sessions[i]));
            lyd_new_list(cont, NULL, "session", 0, &list, buf);

            switch (nc_session_get_ti(stats.sessions[i])) {
#ifdef NC_ENABLED_SSH
            case NC_TI_LIBSSH:
                lyd_new_term(list, NULL, "transport", "netconf-ssh", 0, NULL);
                break;
#endif
#ifdef NC_ENABLED_TLS
            case NC_TI_OPENSSL:
                lyd_new_term(list, NULL, "transport", "netconf-tls", 0, NULL);
                break;
#endif
            default: /* NC_TI_FD, NC_TI_NONE */
                ERR("ietf-netconf-monitoring unsupported session transport type.");
                pthread_mutex_unlock(&stats.lock);
                goto error;
            }
            lyd_new_term(list, NULL, "username", nc_session_get_username(stats.sessions[i]), 0, NULL);
            lyd_new_term(list, NULL, "source-host", nc_session_get_host(stats.sessions[i]), 0, NULL);
            nc_time2datetime(nc_session_get_start_time(stats.sessions[i]), NCM_TIMEZONE, buf);
            lyd_new_term(list, NULL, "login-time", buf, 0, NULL);

            sprintf(buf, "%u", stats.session_stats[i].in_rpcs);
            lyd_new_term(list, NULL, "in-rpcs", buf, 0, NULL);
            sprintf(buf, "%u", stats.session_stats[i].in_bad_rpcs);
            lyd_new_term(list, NULL, "in-bad-rpcs", buf, 0, NULL);
            sprintf(buf, "%u", stats.session_stats[i].out_rpc_errors);
            lyd_new_term(list, NULL, "out-rpc-errors", buf, 0, NULL);
            sprintf(buf, "%u", stats.session_stats[i].out_notifications);
            lyd_new_term(list, NULL, "out-notifications", buf, 0, NULL);
        }
    }

    /* statistics */
    lyd_new_inner(root, NULL, "statistics", 0, &cont);

    nc_time2datetime(stats.netconf_start_time, NCM_TIMEZONE, buf);
    lyd_new_term(cont, NULL, "netconf-start-time", buf, 0, NULL);
    sprintf(buf, "%u", stats.in_bad_hellos);
    lyd_new_term(cont, NULL, "in-bad-hellos", buf, 0, NULL);
    sprintf(buf, "%u", stats.in_sessions);
    lyd_new_term(cont, NULL, "in-sessions", buf, 0, NULL);
    sprintf(buf, "%u", stats.dropped_sessions);
    lyd_new_term(cont, NULL, "dropped-sessions", buf, 0, NULL);
    sprintf(buf, "%u", stats.global_stats.in_rpcs);
    lyd_new_term(cont, NULL, "in-rpcs", buf, 0, NULL);
    sprintf(buf, "%u", stats.global_stats.in_bad_rpcs);
    lyd_new_term(cont, NULL, "in-bad-rpcs", buf, 0, NULL);
    sprintf(buf, "%u", stats.global_stats.out_rpc_errors);
    lyd_new_term(cont, NULL, "out-rpc-errors", buf, 0, NULL);
    sprintf(buf, "%u", stats.global_stats.out_notifications);
    lyd_new_term(cont, NULL, "out-notifications", buf, 0, NULL);

    pthread_mutex_unlock(&stats.lock);

    if (lyd_validate_all(&root, NULL, LYD_VALIDATE_PRESENT, NULL)) {
        goto error;
    }

    *parent = root;
    return SR_ERR_OK;

error:
    lyd_free_tree(root);
    return SR_ERR_INTERNAL;
}
