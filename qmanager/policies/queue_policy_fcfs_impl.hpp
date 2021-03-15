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

#ifndef QUEUE_POLICY_FCFS_IMPL_HPP
#define QUEUE_POLICY_FCFS_IMPL_HPP

#include "qmanager/policies/queue_policy_fcfs.hpp"
#include "qmanager/policies/base/queue_policy_base_impl.hpp"

namespace Flux {
namespace queue_manager {
namespace detail {


/******************************************************************************
 *                                                                            *
 *                    Private Methods of Queue Policy FCFS                    *
 *                                                                            *
 ******************************************************************************/

template<class reapi_type>
int queue_policy_fcfs_t<reapi_type>::cancel_completed_jobs (void *h)
{
    int rc = 0;
    std::shared_ptr<job_t> job;

    // Pop newly completed jobs (e.g., per a free request from job-manager
    // as received by qmanager) to remove them from the resource infrastructure.
    while ((job = complete_pop ()) != nullptr)
        rc += reapi_type::cancel (h, job->id, true);
    return rc;
}

template<class reapi_type>
int queue_policy_fcfs_t<reapi_type>::allocate_jobs (void *h,
                                                    bool use_alloced_queue)
{
    unsigned int i = 0;
    std::shared_ptr<job_t> job;
    std::map<std::vector<double>, flux_jobid_t>::iterator iter;

    // Iterate jobs in the pending job queue and try to allocate each
    // until you can't.
    //
    int saved_errno = errno;
    iter = m_pending.begin ();
    while (iter != m_pending.end () && i < m_queue_depth) {
        errno = 0;
        job = m_jobs[iter->second];
        if (reapi_type::match_allocate (h, false, job->jobspec, job->id,
                                        job->schedule.reserved,
                                        job->schedule.R,
                                        job->schedule.at,
                                        job->schedule.ov) == 0) {
            // move the job to the running queue and make sure the job
            // is enqueued into allocated job queue as well.
            // When this is used within a module (qmanager), it allows the module
            // to fetch those newly allocated jobs, which have flux_msg_t to
            // respond to job-manager.
            iter = to_running (iter, use_alloced_queue);
        } else {
            if (errno != EBUSY) {
                // The request must be rejected. The job is enqueued into
                // rejected job queue to the upper layer to react on this.
                iter = to_rejected (iter, (errno == ENODEV)? "unsatisfiable"
                                                           : "match error");
            }
            else {
                break;
            }
        }
        i++;
    }
    errno = saved_errno;
    return 0;
}

template<class reapi_type>
int queue_policy_fcfs_t<reapi_type>::async_allocate_jobs (void *h,
                                                          bool use_alloced_queue)
{
    std::cout << "async_allocate_jobs" << std::endl;
    int saved_errno = errno ;

    // move jobs in m_pending_provisional queue into
    // m_pending. Note that c++11 doesn't have a clean way
    // to "move" elements between two std::map objects so
    // we use copy for the time being.
    m_pending.insert (m_pending_provisional.begin (),
                      m_pending_provisional.end ());
    m_pending_provisional.clear ();

    m_is_sched_loop_active = true;
    m_iter = m_pending.begin ();
    m_iter_depth = 0;

    if (reapi_type::match_allocate_chain (h, this) < 0) {
        m_is_sched_loop_active = false;
        // TODO: check
    }
    errno = saved_errno;
    return 0;
}


/******************************************************************************
 *                                                                            *
 *                    Public API of Queue Policy FCFS                         *
 *                                                                            *
 ******************************************************************************/

template<class reapi_type>
queue_policy_fcfs_t<reapi_type>::~queue_policy_fcfs_t ()
{

}

template<class reapi_type>
int queue_policy_fcfs_t<reapi_type>::apply_params ()
{
    return queue_policy_base_t::apply_params ();
}

template<class reapi_type>
__attribute__((annotate("@critical_path()")))
int queue_policy_fcfs_t<reapi_type>::run_sched_loop (void *h,
                                                     bool use_alloced_queue)
{
    if (m_is_sched_loop_active)
        return 0;
    std::cout << "run_sched_loop" << std::endl;
    int rc = 0;
    rc = cancel_completed_jobs (h);
    rc += async_allocate_jobs (h, use_alloced_queue);
    return rc;
}

template<class reapi_type>
int queue_policy_fcfs_t<reapi_type>::reconstruct_resource (
        void *h, std::shared_ptr<job_t> job, std::string &R_out)
{
    return reapi_type::update_allocate (h, job->id, job->schedule.R,
                                        job->schedule.at,
                                        job->schedule.ov, R_out);
}

template<class reapi_type>
int queue_policy_fcfs_t<reapi_type>::handle_match_success (
                                         int64_t jobid, const char *status,
                                         const char *R, int64_t at, double ov)
{
    std::cout << "handle_match_success called" << std::endl;
    if (!m_is_sched_loop_active) {
        errno = EINVAL;
        return -1;
    }
    std::shared_ptr<job_t> job = m_jobs[m_iter->second];
    if (job->id != jobid) {
        errno = EINVAL;
        return -1;
    }
    job->schedule.reserved = std::string ("RESERVED") == status?  true : false;
    job->schedule.R = R;
    job->schedule.at = at;
    job->schedule.ov = ov;
    m_iter = to_running (m_iter, true);
    m_iter_depth++;
    return 0;
}

template<class reapi_type>
int queue_policy_fcfs_t<reapi_type>::handle_match_failure (int errcode)
{
    std::cout << "handle_match_failure called" << std::endl;
    if (!m_is_sched_loop_active) {
        errno = EINVAL;
        return -1;
    }
    if (errcode != EBUSY) {
        m_iter = to_rejected (m_iter,
                              (errcode == ENODEV)? "unsatisfiable"
                                                 : "match error");
        m_iter_depth++;
    } else {
        // If you can't schedule, return -1 so that upper layer
        // can break out of schedule loop
        return -1;
    }
    return 0;
}

template<class reapi_type>
int queue_policy_fcfs_t<reapi_type>::get_job_info (std::string &jobspec,
                                                   uint64_t &jobid)
{
    int rc = 0;
    std::cout << "get_job_info" << std::endl;
    if (m_is_sched_loop_active && m_iter != m_pending.end ()) {
        std::shared_ptr<job_t> job = m_jobs[m_iter->second];
        jobspec = job->jobspec;
        jobid = job->id;
        std::cout << "jobid: " << jobid << std::endl;
    } else {
        errno = EINVAL;
        rc = -1;
    }
    return rc;
}

template<class reapi_type>
bool queue_policy_fcfs_t<reapi_type>::has_job_to_consider ()
{
    std::cout << "has_job_to_consider" << std::endl;
    if (m_is_sched_loop_active && (m_iter != m_pending.end ()
                                && m_iter_depth < m_queue_depth)) {
        std::cout << "has_job_to_consider: yes" << std::endl;
        return true;
    }
    return false;
}

template<class reapi_type>
bool queue_policy_fcfs_t<reapi_type>::is_sched_loop_active ()
{
    return m_is_sched_loop_active;
}

template<class reapi_type>
void queue_policy_fcfs_t<reapi_type>::set_sched_loop_active (bool b)
{
    m_is_sched_loop_active = b;
}

template<class reapi_type>
bool queue_policy_fcfs_t<reapi_type>::orelse_reserve ()
{
    return false;
}


} // namespace Flux::queue_manager::detail
} // namespace Flux::queue_manager
} // namespace Flux

#endif // QUEUE_POLICY_FCFS_IMPL_HPP

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
