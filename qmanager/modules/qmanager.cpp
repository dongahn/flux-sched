/*****************************************************************************\
 *  Copyright (c) 2019 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

extern "C" {
#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>
#include "src/common/libschedutil/schedutil.h"
#include "src/common/libeventlog/eventlog.h"
}

#include "qmanager/policies/base/queue_policy_base.hpp"
#include "qmanager/policies/base/queue_policy_base_impl.hpp"
#include "qmanager/policies/queue_policy_factory_impl.hpp"


using namespace Flux;
using namespace Flux::queue_manager;
using namespace Flux::queue_manager::detail;

/******************************************************************************
 *                                                                            *
 *                 Queue Manager Service Module Context                       *
 *                                                                            *
 ******************************************************************************/

struct qmanager_args_t {
    std::string queue_policy;
    std::string queue_params;
    std::string policy_params;
};

struct qmanager_ctx_t {
    flux_t *h;
    qmanager_args_t args;
    ops_context *ops;
    queue_policy_base_t *queue;
};


/******************************************************************************
 *                                                                            *
 *                     Internal Queue Manager APIs                            *
 *                                                                            *
 ******************************************************************************/

static int recover_submitinfo (flux_t *h,
                               const char *e, uint32_t &u, int &p, double &t)
{
    int rc = -1;
    json_t *a = NULL;
    json_t *entry = NULL;
    const char *name = NULL;
    json_t *context = NULL;

    if (!(a = eventlog_decode (e))) {
        flux_log_error (h, "%s: eventlog_decode", __FUNCTION__);
        goto out;
    }
    if (!(entry = json_array_get (a, 0))) {
        errno = EINVAL;
        flux_log_error (h, "%s: json_array_get", __FUNCTION__);
        goto out;
    }
    if (eventlog_entry_parse (entry, &t, &name, &context) < 0) {
        flux_log_error (h, "%s: eventlog_entry_parse", __FUNCTION__);
        goto out;
    }
    if (name != std::string ("submit") || !context) {
        errno = ENOENT;
        flux_log_error (h, "%s: invalid event", __FUNCTION__);
        goto out;
    }
    if (json_unpack (context, "{s:i s:i}", "userid", &u, "priority", &p) < 0) {
        errno = ENOENT;
        flux_log_error (h, "%s: json_unpack", __FUNCTION__);
        goto out;
    }
    rc = 0;

out:
    json_decref (a);
    return rc;
}

static std::shared_ptr<job_t> recover_jobinfo (flux_t *h, flux_jobid_t id,
                                               const char *R)
{
    int prio = 0;
    double ts = 0;
    uint32_t uid = 0;
    char path[64] = {'\0'};
    flux_future_t *f = NULL;
    const char *eventlog = NULL;
    std::shared_ptr<job_t> job = nullptr;

    if (flux_job_kvs_key (path, sizeof (path), id, "eventlog") < 0) {
        flux_log_error (h, "%s: flux_job_kvs_key", __FUNCTION__);
        goto out;
    }
    if (!(f = flux_kvs_lookup (h, NULL, 0, path))) {
        flux_log_error (h, "%s: flux_kvs_lookup", __FUNCTION__);
        goto out;
    }
    if (flux_kvs_lookup_get (f, &eventlog) < 0) {
        flux_log_error (h, "%s: flux_kvs_lookup_get_unpack", __FUNCTION__);
        goto out;
    }
    if (recover_submitinfo (h, eventlog, uid, prio, ts) < 0) {
        flux_log_error (h, "%s: recover_submitinfo", __FUNCTION__);
        goto out;
    }
    job = std::make_shared<job_t> (job_state_kind_t::
                                   RUNNING, id, uid, prio, ts, R);

out:
    flux_future_destroy (f);
    return job;
}

// FIXME: This will be expanded when we implement full scheduler
// resilency schemes: Issue #470.
extern "C" int jobmanager_hello_cb (flux_t *h,
                                    flux_jobid_t id, const char *R, void *arg)
{
    int rc = -1;
    qmanager_ctx_t *ctx = (qmanager_ctx_t *)arg;
    std::shared_ptr<job_t> running_job;
    if ((running_job = recover_jobinfo (h, id, R)) == nullptr) {
        flux_log_error (h, "%s: recover_jobinfo", __FUNCTION__);
        goto out;
    }
    if (ctx->queue->reconstruct (running_job) < 0) {
        flux_log_error (h, "%s: reconstruct (jobid=%ju)",
                        __FUNCTION__, (intmax_t)running_job->id);
        goto out;
    }
    rc = 0;

out:
    return rc;
}

extern "C" void jobmanager_alloc_cb (flux_t *h, const flux_msg_t *msg,
                                     const char *jobspec, void *arg)
{
    uint32_t userid;
    qmanager_ctx_t *ctx = (qmanager_ctx_t *)arg;
    std::shared_ptr<job_t> job = std::make_shared<job_t> ();

    if (flux_msg_get_userid (msg, &userid) < 0)
        return;

    flux_log (h, LOG_INFO, "alloc requested by user (%u).", userid);

    if (schedutil_alloc_request_decode (msg, &job->id, &job->priority,
                                        &job->userid, &job->t_submit) < 0) {
        flux_log_error (h, "%s: schedutil_alloc_request_decode", __FUNCTION__);
        return;
    }
    job->jobspec = jobspec;
    job->msg = flux_msg_copy (msg, true);
    if (ctx->queue->insert (job) < 0) {
        flux_log_error (h, "%s: queue insert", __FUNCTION__);
        return;
    }
    if (ctx->queue->run_sched_loop ((void *)ctx->h, true) < 0) {
        flux_log (ctx->h, LOG_DEBUG,
                  "%s: return code < 0 from schedule loop", __FUNCTION__);
    }
    while ((job = ctx->queue->alloced_pop ()) != nullptr) {
        flux_log (ctx->h, LOG_DEBUG, "jobid (%ju): %s",
                  (intmax_t)job->id, job->schedule.R.c_str ());
        if (schedutil_alloc_respond_R (ctx->h, job->msg,
                                       job->schedule.R.c_str (), NULL) < 0) {
            flux_log_error (ctx->h, "%s: schedutil_alloc_respond_R",
                            __FUNCTION__);
        }
    }
}

extern "C" void jobmanager_free_cb (flux_t *h, const flux_msg_t *msg,
                                    const char *R, void *arg)
{
    uint32_t userid;
    flux_jobid_t id;
    qmanager_ctx_t *ctx = (qmanager_ctx_t *)arg;
    std::shared_ptr<job_t> job;

    if (flux_msg_get_userid (msg, &userid) < 0)
        return;

    flux_log (h, LOG_INFO, "free requested by user (%u).", userid);

    if (schedutil_free_request_decode (msg, &id) < 0) {
        flux_log_error (h, "%s: schedutil_free_request_decode",
                        __FUNCTION__);
        return;
    }
    if ((ctx->queue->remove (id)) < 0)
        flux_log_error (h, "%s: remove job (%ju)", __FUNCTION__, (intmax_t)id);
    if (ctx->queue->run_sched_loop ((void *)ctx->h, true) < 0) {
        // TODO: Need to tighten up anomalous conditions
        // returned with a negative return code
        // (e.g., unsatisfiable jobs).
        flux_log (ctx->h, LOG_DEBUG,
                  "%s: return code < 0 from schedule loop", __FUNCTION__);
    }
    if (schedutil_free_respond (h, msg) < 0) {
        flux_log_error (h, "%s: schedutil_free_respond", __FUNCTION__);
    }
    while ((job = ctx->queue->alloced_pop ()) != nullptr) {
        flux_log (ctx->h, LOG_DEBUG, "jobid (%ju): %s",
                  (intmax_t)job->id, job->schedule.R.c_str ());
        if (schedutil_alloc_respond_R (ctx->h, job->msg,
                                       job->schedule.R.c_str (), NULL) < 0) {
            flux_log_error (ctx->h, "%s: schedutil_alloc_respond_R",
                            __FUNCTION__);
        }
    }
}

static void jobmanager_exception_cb (flux_t *h, flux_jobid_t id,
                                     const char *t, int s, void *a)
{
    std::shared_ptr<job_t> job;
    qmanager_ctx_t *ctx = (qmanager_ctx_t *)a;

    if (s > 0 || (job = ctx->queue->lookup (id)) == nullptr
        || !job->is_pending ())
        return;
    if (ctx->queue->remove (id) < 0) {
        flux_log_error (h, "%s: remove job (%ju)", __FUNCTION__, (intmax_t)id);
        return;
    }
    std::string note = std::string ("alloc aborted due to exception type=") + t;
    if (schedutil_alloc_respond_denied (h, job->msg, note.c_str ()) < 0) {
        flux_log_error (h, "%s: schedutil_alloc_respond_denied", __FUNCTION__);
    }
}

static int process_args (qmanager_ctx_t *ctx, int argc, char **argv)
{
    int rc = 0;
    qmanager_args_t &args = ctx->args;
    std::string dflt = "";

    for (int i = 0; i < argc; i++) {
        if (!strncmp ("queue-policy=", argv[i], sizeof ("queue-policy"))) {
            dflt = args.queue_policy;
            args.queue_policy = strstr (argv[i], "=") + 1;
            if (!known_queue_policy (args.queue_policy)) {
                flux_log (ctx->h, LOG_ERR,
                          "Unknown queuing policy (%s)! Use default (%s).",
                           args.queue_policy.c_str (), dflt.c_str ());
                args.queue_policy = dflt;
            }
        }
        else if (!strncmp ("queue-params=", argv[i], sizeof ("queue-params"))) {
            args.queue_params = strstr (argv[i], "=") + 1;
        }
        else if (!strncmp ("policy-params=", argv[i],
                               sizeof ("policy-params"))) {
            args.policy_params = strstr (argv[i], "=") + 1;
        }
    }

    return rc;
}

static void set_default_args (qmanager_args_t &args)
{
    args.queue_policy = "fcfs";
    args.policy_params = "";
}

int enforce_queue_policy (qmanager_ctx_t *ctx)
{
    int rc = -1;
    int queue_depth = 0;
    const char *jm_mode = "single";
    ctx->queue = create_queue_policy (ctx->args.queue_policy, "module");
    if (!ctx->queue) {
        flux_log_error (ctx->h, "%s: create_queue_policy", __FUNCTION__);
        goto out;
    }
    if (ctx->args.policy_params != ""
        && ctx->queue->set_params (ctx->args.policy_params) < 0) {
        flux_log_error (ctx->h, "%s: queue->set_params", __FUNCTION__);
        goto out;
    }
    if (ctx->args.queue_params != ""
        && ctx->queue->set_params (ctx->args.queue_params) < 0) {
        flux_log_error (ctx->h, "%s: queue->set_params", __FUNCTION__);
        goto out;
    }
    if (ctx->queue->apply_params () < 0) {
        flux_log_error (ctx->h, "%s: queue->apply_params", __FUNCTION__);
        goto out;
    }
    if (!(ctx->ops = schedutil_ops_register (ctx->h,
                                             jobmanager_alloc_cb,
                                             jobmanager_free_cb,
                                             jobmanager_exception_cb, ctx))) {
        flux_log_error (ctx->h, "%s: schedutil_ops_register", __FUNCTION__);
        goto out;
    }
    if (schedutil_hello (ctx->h, jobmanager_hello_cb, ctx) < 0) {
        flux_log_error (ctx->h, "%s: schedutil_hello", __FUNCTION__);
        goto out;
    }
    if (ctx->args.queue_policy != "fcfs")
        jm_mode = "unlimited";
    if (schedutil_ready (ctx->h, jm_mode, &queue_depth)) {
        flux_log_error (ctx->h, "%s: schedutil_ready", __FUNCTION__);
        goto out;
    }
    rc = 0;
out:
    return rc;
}

static qmanager_ctx_t *qmanager_new (flux_t *h)
{
    qmanager_ctx_t *ctx = NULL;

    if (!(ctx = new (std::nothrow) qmanager_ctx_t ())) {
        errno = ENOMEM;
        goto out;
    }
    ctx->h = h;
    set_default_args (ctx->args);
    ctx->queue = nullptr;
out:
    return ctx;
}

static void qmanager_destroy (qmanager_ctx_t *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        std::shared_ptr<job_t> job;
        while ((job = ctx->queue->pending_pop ()) != nullptr)
            flux_respond_error (ctx->h, job->msg, ENOSYS, "unloading");
        while ((job = ctx->queue->complete_pop ()) != nullptr)
            flux_respond_error (ctx->h, job->msg, ENOSYS, "unloading");
        delete ctx->queue;
        ctx->queue = NULL;
        schedutil_ops_unregister (ctx->ops);
        free (ctx);
        errno = saved_errno;
    }
}


/******************************************************************************
 *                                                                            *
 *                               Module Main                                  *
 *                                                                            *
 ******************************************************************************/

extern "C" int mod_main (flux_t *h, int argc, char **argv)
{
    int rc = -1;
    try {
        qmanager_ctx_t *ctx = NULL;
        if (!(ctx = qmanager_new (h))) {
            flux_log_error (h, "%s: qmanager_new", __FUNCTION__);
            return rc;
        }
        if ((rc = process_args (ctx, argc, argv)) < 0) {
            flux_log_error (h, "%s: load line argument parsing", __FUNCTION__);
            qmanager_destroy (ctx);
            return rc;
        }
        if ((rc = enforce_queue_policy (ctx)) < 0) {
            flux_log_error (h, "%s: enforce_queue_policy", __FUNCTION__);
            qmanager_destroy (ctx);
            return rc;
        }
        if ((rc = flux_reactor_run (flux_get_reactor (h), 0)) < 0)
            flux_log_error (h, "%s: flux_reactor_run", __FUNCTION__);
        qmanager_destroy (ctx);
    }
    catch (std::exception &e) {
        flux_log_error (h, "%s: %s", __FUNCTION__, e.what ());
    }
    return rc;
}

MOD_NAME ("qmanager");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
