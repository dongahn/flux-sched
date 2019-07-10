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

#ifndef QUEUE_POLICY_BASE_HPP
#define QUEUE_POLICY_BASE_HPP

extern "C" {
#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
}

#include <map>
#include <string>
#include <memory>
#include <cstdint>

namespace Flux {
namespace queue_manager {

enum class job_state_kind_t { INIT,
                              PENDING,
                              RUNNING,
                              ALLOC_RUNNING,
                              CANCELED,
                              COMPLETE };

/*! Type to store schedule information such as the
 *  allocated or reserved (for backfill) resource set (R).
 */
struct schedule_t {
    std::string R = "";
    bool reserved = false;
    int64_t at = 0;
    double ov = 0.0f;
};

/*! Type to store various time stamps for queuing
 */
struct t_stamps_t {
    uint64_t pending_ts = 0;
    uint64_t running_ts = 0;
    uint64_t complete_ts = 0;
};

/*! Type to store a job's attributes.
 */
class job_t {
public:
    ~job_t () { flux_msg_destroy (msg); }
    flux_msg_t *msg = NULL;
    job_state_kind_t state = job_state_kind_t::INIT;
    flux_jobid_t id = 0;
    uint32_t userid = 0;
    int priority = 0;
    double t_submit = 0.0f;;
    std::string jobspec = "";
    t_stamps_t t_stamps;
    schedule_t schedule;
};


namespace detail {
class queue_policy_base_impl_t
{
public:
    int insert (std::shared_ptr<job_t> job);
    int remove (flux_jobid_t id);

protected:
    std::shared_ptr<job_t> pending_pop ();
    std::shared_ptr<job_t> alloced_pop ();
    std::shared_ptr<job_t> complete_pop ();
    std::map<uint64_t, flux_jobid_t>::iterator to_running (
        std::map<uint64_t, flux_jobid_t>::iterator pending_iter,
        bool use_alloced_queue);
    std::map<uint64_t, flux_jobid_t>::iterator to_complete (
        std::map<uint64_t, flux_jobid_t>::iterator running_iter);

    uint64_t m_pq_cnt = 0;
    uint64_t m_rq_cnt = 0;
    uint64_t m_cq_cnt = 0;
    uint64_t m_oq_cnt = 0;
    std::map<uint64_t, flux_jobid_t> m_pending;
    std::map<uint64_t, flux_jobid_t> m_running;
    std::map<uint64_t, flux_jobid_t> m_alloced;
    std::map<uint64_t, flux_jobid_t> m_complete;
    std::map<flux_jobid_t, std::shared_ptr<job_t>> m_jobs;
};
} // namespace Flux::queue_manager::detail


/*! Queue policy base interface abstract class. Derived classes must
 *  implement its run_sched_loop and destructor methods. Insert, remove
 *  and pending_pop interface implementations are provided through
 *  its parent class (detail::queue_policy_base_impl_t).
 */
class queue_policy_base_t : public detail::queue_policy_base_impl_t
{
public:
    /*! The destructor that must be implemented by derived classes.
     *
     */
    virtual ~queue_policy_base_t () {};

    /*! The main schedule loop interface that must be implemented
     *  by derived classes: how a derived class implements this method
     *  must determine its queueing policy. For example, a pedantic FCFS
     *  class should only iterate through the first set of jobs in the
     *  pending-job queue which it can schedule using the underlying
     *  resource match infrastructure.
     *
     *  \param h         Opaque handle. How it is used is an implementation
     *                   detail. However, when it is used within a Flux's
     *                   service module such as qmanager, it is expected
     *                   to be a pointer to a flux_t object.
     *  \param use_alloced_queue
     *                   Boolean indicating if you want to use the
     *                   allocated job queue or not. This affects the
     *                   alloced_pop method.
     *  \return          0 on success; -1 on error.
     *                       EINVAL: invalid argument.
     */
    virtual int run_sched_loop (void *h, bool use_alloced_queue) = 0;

    /*! Append a job into the internal pending-job queue.
     *
     *  \param job       a shared pointer pointing to a job_t object.
     *  \return          0 on success; -1 on error.
     *                       EINVAL: invalid argument.
     */
    int insert (std::shared_ptr<job_t> job);

    /*! Remove a job whose jobid is id from any internal queues
     *  (e.g., pending queue, running queue, and alloced queue.)
     *
     *  \param id        jobid of flux_jobid_t type.
     *  \return          0 on success; -1 on error.
     */
    int remove (flux_jobid_t id);

    /*! Pop the first job from the pending job queue. The popped
     *  job is completely graduated from the queue policy layer.
     *
     *  \return          a shared pointer pointing to a job_t object
     *                   on success; nullptr when the queue is empty.
     */
    std::shared_ptr<job_t> pending_pop ();

    /*! Pop the first job from the alloced job queue. The popped
     *  job still remains in the queue policy layer (i.e., in the
     *  internal running job queue).
     *  \return          a shared pointer pointing to a job_t object
     *                   on success; nullptr when the queue is empty.
     */
    std::shared_ptr<job_t> alloced_pop ();

    /*! Pop the first job from the internal completed job queue.
     *  The popped is completely graduated from the queue policy layer.
     *  \return          a shared pointer pointing to a job_t object
     *                   on success; nullptr when the queue is empty.
     */
    std::shared_ptr<job_t> complete_pop ();
};

} // namespace Flux::queue_manager
} // namespace Flux

#endif // QUEUE_POLICY_BASE_HPP

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
