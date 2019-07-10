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

#ifndef QUEUE_POLICY_BF_BASE_IMPL_HPP
#define QUEUE_POLICY_BF_BASE_IMPL_HPP

#include "qmanager/policies/queue_policy_bf_base.hpp"

namespace Flux {
namespace queue_manager {
namespace detail {

/******************************************************************************
 *                                                                            *
 *                 Private Methods of Queue Policy Backfill Base              *
 *                                                                            *
 ******************************************************************************/

template<class reapi_type>
int queue_policy_bf_base_t<reapi_type>::cancel_completed_jobs (void *h)
{
    int rc = 0;
    std::shared_ptr<job_t> job;

    // Pop newly completed jobs (e.g., per a free request from job-manager
    // as received by qmanager) to remove them from the resource infrastructure.
    while ((job = complete_pop ()) != nullptr)
        rc += reapi_type::cancel (h, job->id);
    return rc;
}

template<class reapi_type>
int queue_policy_bf_base_t<reapi_type>::cancel_reserved_jobs (void *h)
{
    int rc = 0;
    std::map<uint64_t, flux_jobid_t>::const_iterator citer;
    for (citer = m_reserved.begin (); citer != m_reserved.end (); citer++)
        rc += reapi_type::cancel (h, citer->second);
    m_reserved.clear ();
    return rc;
}

template<class reapi_type>
int queue_policy_bf_base_t<reapi_type>::allocate_orelse_reserve_jobs (void *h,
                                            bool use_alloced_queue)
{
    int rc = 0;
    unsigned int i = 0;
    std::shared_ptr<job_t> job;
    unsigned int reservation_cnt = 0;
    std::map<uint64_t, flux_jobid_t>::iterator iter;

    // Iterate jobs in the pending job queue and try to allocate each
    // until you can't or queue depth limit reached.
    // When you can't allocate a job, you reserve it and then try
    // to backfill later jobs.
    for (i = 0, iter = m_pending.begin ();
         i < m_queue_depth && iter != m_pending.end (); i++) {
        bool orelse = (reservation_cnt < m_reservation_depth)? false : true;
        job = m_jobs[iter->second];
        if ((rc = reapi_type::match_allocate (h, orelse,
                                              job->jobspec, job->id,
                                              job->schedule.reserved,
                                              job->schedule.R, job->schedule.at,
                                              job->schedule.ov) < 0)) {
            break;
        }

        if (job->schedule.reserved) {
            reservation_cnt++;
            if (reservation_cnt > m_reservation_depth) {
                rc = -1;
                break;
            }
            m_reserved.insert (std::pair<uint64_t, flux_jobid_t> (m_oq_cnt++,
                                                                  job->id));
        } else {
            // move the job to the running queue and make sure the job
            // is enqueued into allocated job queue as well.
            // When this is used within a module, it allows the module
            // to fetch those newly allocated jobs, which have flux_msg_t to
            // respond to job-manager.
            iter = to_running (iter, use_alloced_queue);
       }
    }
    return rc;
}


/******************************************************************************
 *                                                                            *
 *                 Public API of Queue Policy Backfill Base                   *
 *                                                                            *
 ******************************************************************************/

template<class reapi_type>
queue_policy_bf_base_t<reapi_type>::~queue_policy_bf_base_t ()
{

}

template<class reapi_type>
int queue_policy_bf_base_t<reapi_type>::apply_params ()
{
    return 0;
}

template<class reapi_type>
int queue_policy_bf_base_t<reapi_type>::run_sched_loop (void *h,
                                                        bool use_alloced_queue)
{
    int rc = 0;
    rc = cancel_completed_jobs (h);
    rc += allocate_orelse_reserve_jobs (h, use_alloced_queue);
    rc += cancel_reserved_jobs (h);
    return rc;
}

} // namespace Flux::queue_manager::detail
} // namespace Flux::queue_manager
} // namespace Flux

#endif // QUEUE_POLICY_BF_BASE_IMPL_HPP

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
