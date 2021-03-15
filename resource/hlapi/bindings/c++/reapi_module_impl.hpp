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

#ifndef REAPI_MODULE_IMPL_HPP
#define REAPI_MODULE_IMPL_HPP

extern "C" {
#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
}

#include <iostream>
#include <cerrno>
#include "resource/hlapi/bindings/c++/reapi_module.hpp"

namespace Flux {
namespace resource_model {
namespace detail {


__attribute__((annotate ("@critical_path(flow='outin')")))
int reapi_module_t::match_allocate (void *h, bool orelse_reserve,
                                    const std::string &jobspec,
                                    const uint64_t jobid, bool &reserved,
                                    std::string &R, int64_t &at, double &ov)
{
    int rc = -1;
    int64_t rj = -1;
    flux_t *fh = (flux_t *)h;
    flux_future_t *f = NULL;
    const char *rset = NULL;
    const char *status = NULL;
    const char *cmd = (orelse_reserve)? "allocate_orelse_reserve"
                                      : "allocate_with_satisfiability";

    if (!fh || jobspec == "" || jobid > INT64_MAX) {
        errno = EINVAL;
        goto out;
    }

    if (!(f = flux_rpc_pack (fh, "sched-fluxion-resource.match",
                             FLUX_NODEID_ANY, 0,
                             "{s:s s:I s:s}",
                             "cmd", cmd, "jobid", (const int64_t)jobid,
                             "jobspec", jobspec.c_str ()))) {
        goto out;
    }

    if (flux_rpc_get_unpack (f, "{s:I s:s s:f s:s s:I}",
                             "jobid", &rj, "status", &status,
                             "overhead", &ov, "R", &rset, "at", &at) < 0) {
        goto out;
    }
    reserved = (std::string ("RESERVED") == status)? true : false;
    R = rset;
    if (rj != (int64_t)jobid) {
        errno = EINVAL;
        goto out;
    }
    rc = 0;

out:
    flux_future_destroy (f);
    return rc;
}

__attribute__((annotate ("@critical_path(flow='in')")))
static void match_allocate_chain_cont (flux_future_t *f, void *arg)
{
    int64_t rj = -1;
    int64_t at;
    double ov;
    const char *rset = nullptr;
    const char *status = nullptr;
    queue_adapter_base_t *adapter = static_cast<queue_adapter_base_t *> (arg);

    std::cout << "match_allocate_chain_cont called" << std::endl;

    if (flux_rpc_get_unpack (f,
                             "{s:I s:s s:f s:s s:I}",
                              "jobid", &rj,
                              "status", &status,
                              "overhead", &ov,
                              "R", &rset,
                              "at", &at) == 0) {
        std::cout << "flux_rpc_get_unpack returns" << std::endl;
        // match succeeded. handle_match_success should move the current
        // job to the next job to consider.
        if (adapter->handle_match_success (rj, status, rset, at, ov) < 0) {
            adapter->set_sched_loop_active (false);
            goto out;
        }
    } else {
        std::cout << "flux_rpc_get_unpack else stmt" << std::endl;
        // match succeeded. handle_match_success should move the current
        // job to the next job to conside unless errno == EBUSY
        if (adapter->handle_match_failure (errno) < 0) {
            adapter->set_sched_loop_active (false);
            goto out;
        }
    }
    std::cout << "about to call match_allocate_chain" << std::endl;
    if (reapi_module_t::match_allocate_chain (
                            flux_future_get_flux (f), adapter) < 0) {
        adapter->set_sched_loop_active (false);
        goto out;
    }

out:
    flux_future_destroy (f);
    return;
}

__attribute__((annotate ("@critical_path(flow='out')")))
static flux_future_t *match_allocate_rpc (flux_t *h,
                                          const char *cmd, 
                                          const int64_t jobid,
                                          const char *jobspec)
{
    flux_future_t *f;
    if (!(f = flux_rpc_pack (h,
                             "sched-fluxion-resource.match", FLUX_NODEID_ANY, 0,
                             "{s:s s:I s:s}",
                              "cmd", cmd,
                              "jobid", jobid,
                              "jobspec", jobspec))) {
        return nullptr;
    }
    return f;
}

int reapi_module_t::match_allocate_chain (void *h,
                                          queue_adapter_base_t *adapter)
{
    int rc = -1;
    uint64_t jobid = 0;
    std::string jobspec;
    flux_t *fh = static_cast<flux_t *> (h);
    flux_future_t *f = nullptr;

    std::cout << "match_allocate_chain called" << std::endl;

    if (!adapter->has_job_to_consider ()) {
        // break out of the chain of match_allocate calls
        adapter->set_sched_loop_active (false);
        return 0;
    }

    const char *cmd = (adapter->orelse_reserve ())
                          ? "allocate_orelse_reserve"
                          : "allocate_with_satisfiability";
    if (adapter->get_job_info (jobspec, jobid) < 0) {
        goto error;
    }
    if (!fh || jobspec == "" || jobid > INT64_MAX) {
        errno = EINVAL;
        goto error;
    }
    std::cout << "about to send sched-fluxion-resource.match" << std::endl;
    if (!(f = match_allocate_rpc (fh, cmd,
                                  static_cast<const int64_t> (jobid),
                                  jobspec.c_str ()))) {
        goto error;
    }
    std::cout << "sched-fluxion-resource.match return" << std::endl;
    if (flux_future_then (f,
                          -1.0f,
                          match_allocate_chain_cont,
                          static_cast<void *> (adapter)) < 0) {
        goto error;
    }
    std::cout << "flux_future_then return" << std::endl;

    return 0;

error:
    flux_future_destroy (f);
    return rc;
}

int reapi_module_t::update_allocate (void *h, const uint64_t jobid,
                                    const std::string &R, int64_t &at,
                                    double &ov, std::string &R_out)
{
    int rc = -1;
    int64_t rj = -1;
    flux_t *fh = (flux_t *)h;
    flux_future_t *f = NULL;
    int64_t scheduled_at = -1;
    double overhead = 0.0f;
    const char *rset = NULL;
    const char *status = NULL;

    if (!fh || R == "" || jobid > INT64_MAX) {
        errno = EINVAL;
        goto out;
    }
    if ( !(f = flux_rpc_pack (fh, "sched-fluxion-resource.update",
                                  FLUX_NODEID_ANY, 0,
                                  "{s:I s:s}",
                                      "jobid", jobid,
                                      "R", R.c_str ())))
        goto out;
    if ( (rc = flux_rpc_get_unpack (f, "{s:I s:s s:f s:s s:I}",
                                           "jobid", &rj,
                                           "status", &status,
                                           "overhead", &overhead,
                                           "R", &rset,
                                           "at", &scheduled_at)) < 0)
        goto out;
    if (rj != static_cast<int64_t> (jobid)
        || rset == NULL
        || status == NULL
        || std::string ("ALLOCATED") != status) {
        rc = -1;
        errno = EPROTO;
        goto out;
    }
    R_out = rset;
    ov = overhead;
    at = scheduled_at;

out:
    flux_future_destroy (f);
    return rc;
}

int reapi_module_t::cancel (void *h, const uint64_t jobid, bool noent_ok)
{
    int rc = -1;
    flux_t *fh = (flux_t *)h;
    flux_future_t *f = NULL;
    int saved_errno;

    if (!fh || jobid > INT64_MAX) {
        errno = EINVAL;
        goto out;
    }
    if (!(f = flux_rpc_pack (fh, "sched-fluxion-resource.cancel",
                             FLUX_NODEID_ANY, 0,
                             "{s:I}", "jobid", (const int64_t)jobid))) {
        goto out;
    }
    saved_errno = errno;
    if ((rc = flux_rpc_get (f, NULL)) < 0) {
        if (noent_ok && errno == ENOENT) {
            errno = saved_errno;
            rc = 0;
	}
        goto out;
    }
    rc = 0;

out:
    flux_future_destroy (f);
    return rc;
}

int reapi_module_t::info (void *h, const uint64_t jobid,
                          bool &reserved, int64_t &at, double &ov)
{
    int rc = -1;
    int64_t rj = -1;
    flux_t *fh = (flux_t *)h;
    flux_future_t *f = NULL;
    const char *status = NULL;

    if (!fh || jobid > INT64_MAX) {
        errno = EINVAL;
        goto out;
    }
    if (!(f = flux_rpc_pack (fh, "sched-fluxion-resource.info",
                             FLUX_NODEID_ANY, 0,
                             "{s:I}", "jobid", (const int64_t)jobid))) {
        goto out;
    }
    if (flux_rpc_get_unpack (f, "{s:I s:s s:I s:f}",
                             "jobid", &rj, "status", &status,
                             "at", &at, "overhead", &ov) < 0) {
        goto out;
    }
    reserved = (std::string ("RESERVED") == status)? true : false;
    if (rj != (int64_t)jobid) {
        errno = EINVAL;
        goto out;
    }
    rc = 0;

out:
    flux_future_destroy (f);
    return rc;
}

int reapi_module_t::stat (void *h, int64_t &V, int64_t &E,int64_t &J,
                          double &load, double &min, double &max, double &avg)
{
    int rc = -1;
    flux_t *fh = (flux_t *)h;
    flux_future_t *f = NULL;

    if (!fh) {
        errno = EINVAL;
        goto out;
    }

    if (!(f = flux_rpc (fh, "sched-fluxion-resource.stat",
                        NULL, FLUX_NODEID_ANY, 0))) {
        goto out;
    }
    if ((rc = flux_rpc_get_unpack (f, "{s:I s:I s:f s:I s:f s:f s:f}",
                                   "V", &V, "E", &E, "load-time", &load,
                                   "njobs", &J, "min-match", &min,
                                   "max-match", &max, "avg-match", &avg)) < 0) {
        goto out;
    }

out:
    flux_future_destroy (f);
    return rc;
}

} // namespace Flux::resource_model::detail
} // namespace Flux::resource_model
} // namespace Flux

#endif // REAPI_MODULE_IMPL_HPP

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
