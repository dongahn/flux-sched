/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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

#include <cstdbool>
#include <czmq.h>
#include <map>

#include "planner_internal_tree.hpp"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "planner.h"

typedef struct span span_t;

typedef struct request {
    int64_t on_or_after;
    uint64_t duration;
    int64_t count;
} request_t;

/*! Node in a span interval tree to enable fast retrieval of intercepting spans.
 */
struct span {
    int64_t start;               /* start time of the span */
    int64_t last;                /* end time of the span */
    int64_t span_id;             /* unique span id */
    int64_t planned;             /* required resource quantity */
    int in_system;               /* 1 when inserted into the system */
    scheduled_point_t *start_p;  /* scheduled point object at start */
    scheduled_point_t *last_p;   /* scheduled point object at last */
};

/*! Planner context
 */
struct planner {
    int64_t total_resources;
    char *resource_type;
    int64_t plan_start;          /* base time of the planner */
    int64_t plan_end;            /* end time of the planner */
    scheduled_point_tree_t sched_point_tree;  /* scheduled point rb tree */
    mintime_resource_tree_t mt_resource_tree;  /* min-time resrouce rb tree */
    scheduled_point_t *p0;       /* system's scheduled point at base time */
    zhashx_t *span_lookup;       /* span lookup table by string id */
    //zhashx_t *avail_time_iter;    /* tracking nodes temporarily deleted from MTR */
    std::map<int64_t, scheduled_point_t *> avail_time_iter;
    request_t *current_request;  /* the req copy for avail time iteration */
    int avail_time_iter_set;     /* iterator set flag */
    uint64_t span_counter;       /* current span counter */
};

/*******************************************************************************
 *                                                                             *
 *                  Scheduled Point and Resource Update APIs                   *
 *                                                                             *
 *******************************************************************************/
static int track_points (std::map<int64_t, scheduled_point_t *> &tracker,
                        scheduled_point_t *point)
{
    //char key[32];
    //sprintf (key, "%jd", (intmax_t)point->at);
    // caller will rely on the fact that rc == -1 when key already exists.
    // don't need to register free */
    if (tracker.find (point->at) != tracker.end ())
        return -1;
    tracker[point->at] = point;
    return 0;
}

static void restore_track_points (planner_t *ctx)
{
    scheduled_point_t *point = NULL;
    for (auto &kv : ctx->avail_time_iter)
        ctx->mt_resource_tree.insert (kv.second);
    ctx->avail_time_iter.clear ();
}

static void update_mintime_resource_tree (planner_t *ctx, zlist_t *list)
{
    scheduled_point_t *point = NULL;
    for (point = static_cast<scheduled_point_t *> (zlist_first (list));
         point; point = static_cast<scheduled_point_t *> (zlist_next (list))) {
        if (point->in_mt_resource_tree)
            ctx->mt_resource_tree.remove (point);
        if (point->ref_count && !(point->in_mt_resource_tree))
            ctx->mt_resource_tree.insert (point);
    }
}

static void copy_req (request_t *dest, int64_t on_or_after, uint64_t duration,
                      uint64_t resource_count)
{
    dest->on_or_after = on_or_after;
    dest->duration = duration;
    dest->count = (int64_t)resource_count;
}

static scheduled_point_t *get_or_new_point (planner_t *ctx, int64_t at)
{
    scheduled_point_t *point = NULL;
    if ( !(point = ctx->sched_point_tree.search (at))) {
        scheduled_point_t *state = ctx->sched_point_tree.get_state (at);
        point = new scheduled_point_t ();
        point->at = at;
        point->in_mt_resource_tree = 0;
        point->new_point = 1;
        point->ref_count = 0;
        point->scheduled = state->scheduled;
        point->remaining = state->remaining;
        ctx->sched_point_tree.insert (point);
        ctx->mt_resource_tree.insert (point);
    }
    return point;
}

static void fetch_overlap_points (planner_t *ctx, int64_t at, uint64_t duration,
                                  zlist_t *list)
{
    scheduled_point_t *point = ctx->sched_point_tree.get_state (at);
    while (point) {
        if (point->at >= (at + (int64_t)duration))
            break;
        else if (point->at >= at)
            zlist_append (list, (void *)point);
        point = ctx->sched_point_tree.next (point);
    }
}

static int update_points_add_span (planner_t *ctx, zlist_t *list, span_t *span)
{
    int rc = 0;
    scheduled_point_t *point = NULL;
    for (point = static_cast<scheduled_point_t *> (zlist_first (list));
         point; point = static_cast<scheduled_point_t *> (zlist_next (list))) {
        point->scheduled += span->planned;
        point->remaining -= span->planned;
        if ( (point->scheduled > ctx->total_resources)
              || (point->remaining < 0)) {
            errno = ERANGE;
            rc = -1;
        }
    }
    return rc;
}

static int update_points_subtract_span (planner_t *ctx, zlist_t *list,
                                        span_t *span)
{
    int rc = 0;
    scheduled_point_t *point = NULL;
    for (point = static_cast<scheduled_point_t *> (zlist_first (list));
         point; point = static_cast<scheduled_point_t *> (zlist_next (list))) {
        point->scheduled -= span->planned;
        point->remaining += span->planned;
        if ( (point->scheduled < 0)
              || (point->remaining > ctx->total_resources)) {
            errno = ERANGE;
            rc = -1;
        }
    }
    return rc;
}

static bool span_ok (planner_t *ctx, scheduled_point_t *start_point,
                     uint64_t duration, int64_t request)
{
    bool ok = true;
    scheduled_point_t *next_point;
    for (next_point = start_point;
         next_point != NULL;
         next_point = ctx->sched_point_tree.next (next_point)) {
         if (next_point->at >= (start_point->at + (int64_t)duration)) {
             ok = true;
             break;
         } else if (request > next_point->remaining) {
             ctx->mt_resource_tree.remove (start_point);
             track_points (ctx->avail_time_iter, start_point);
             ok = false;
             break;
         }
    }
    return ok;
}

static int64_t avail_at (planner_t *ctx, int64_t on_or_after, uint64_t duration,
                         int64_t request)
{
    int64_t at = -1;
    scheduled_point_t *start_point = NULL;
    while ((start_point = ctx->mt_resource_tree.get_mintime (request))) {
        at = start_point->at;
        if (at < on_or_after) {
            ctx->mt_resource_tree.remove (start_point);
            track_points (ctx->avail_time_iter, start_point);
            at = -1;

        } else if (span_ok (ctx, start_point, duration, request)) {
            ctx->mt_resource_tree.remove (start_point);
            track_points (ctx->avail_time_iter, start_point);
            if (static_cast<int64_t> (at + duration) > ctx->plan_end)
                at = -1;
            break;
        }
    }
    return at;
}

static bool avail_during (planner_t *ctx, int64_t at, uint64_t duration,
                          const int64_t request)
{
    bool ok = true;
    if (static_cast<int64_t> (at + duration) > ctx->plan_end) {
        errno = ERANGE;
        return -1;
    }

    scheduled_point_t *point = ctx->sched_point_tree.get_state (at);
    while (point) {
        if (point->at >= (at + (int64_t)duration)) {
            ok = true;
            break;
        } else if (request > point->remaining) {
            ok = false;
            break;
        }
        point = ctx->sched_point_tree.next (point);
    }
    return ok;
}

static scheduled_point_t *avail_resources_during (planner_t *ctx, int64_t at,
                                                  uint64_t duration)
{
    if (static_cast<int64_t> (at + duration) > ctx->plan_end) {
        errno = ERANGE;
        return NULL;
    }

    scheduled_point_t *point = ctx->sched_point_tree.get_state (at);
    scheduled_point_t *min = point;
    while (point) {
        if (point->at >= (at + (int64_t)duration))
            break;
        else if (min->remaining > point->remaining)
          min = point;
        point = ctx->sched_point_tree.next (point);
    }
    return min;
}


/*******************************************************************************
 *                                                                             *
 *                              Utilities                                      *
 *                                                                             *
 *******************************************************************************/

static void initialize (planner_t *ctx, int64_t base_time, uint64_t duration)
{
    ctx->plan_start = base_time;
    ctx->plan_end = base_time + (int64_t)duration;
    ctx->p0 = new scheduled_point_t ();
    ctx->p0->at = base_time;
    ctx->p0->ref_count = 1;
    ctx->p0->remaining = ctx->total_resources;
    ctx->sched_point_tree.insert (ctx->p0);
    ctx->mt_resource_tree.insert (ctx->p0);
    ctx->span_lookup = zhashx_new ();
    ctx->current_request = new request_t ();
    ctx->avail_time_iter_set = 0;
    ctx->span_counter = 0;
}

static inline void erase (planner_t *ctx)
{
    if (ctx->span_lookup)
        zhashx_purge (ctx->span_lookup);
    zhashx_destroy (&(ctx->span_lookup));

    ctx->avail_time_iter.clear ();
    if (ctx->current_request) {
        free (ctx->current_request);
        ctx->current_request = NULL;
    }
    if (ctx->p0 && ctx->p0->in_mt_resource_tree)
        ctx->mt_resource_tree.remove (ctx->p0);
    ctx->sched_point_tree.destroy ();
}

static inline bool not_feasable (planner_t *ctx, int64_t start_time,
                                 uint64_t duration, int64_t request)
{
    bool rc = (start_time < ctx->plan_start) || (duration < 1)
              || ((start_time + static_cast<int64_t> (duration) - 1)
                      > ctx->plan_end);
    return rc;
}

static int span_input_check (planner_t *ctx, int64_t start_time,
                             uint64_t duration, int64_t request)
{
    int rc = -1;
    if (!ctx || not_feasable (ctx, start_time, duration, request)) {
        errno = EINVAL;
        goto done;
    } else if (request > ctx->total_resources || request < 0) {
        errno = ERANGE;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

static span_t *span_new (planner_t *ctx, int64_t start_time, uint64_t duration,
                         uint64_t request)
{
    char key[32];
    span_t *span = NULL;
    if (span_input_check (ctx, start_time, duration, (int64_t)request) == -1)
        goto done;

    span = new span_t ();
    span->start = start_time;
    span->last = start_time + duration;
    ctx->span_counter++;
    span->span_id = ctx->span_counter;
    span->planned = request;
    span->in_system = 0;
    span->start_p = NULL;
    span->last_p = NULL;
    sprintf (key, "%jd", (intmax_t)span->span_id);
    zhashx_insert (ctx->span_lookup, key, span);
    zhashx_freefn (ctx->span_lookup, key, free);
done:
    return span;
}


/*******************************************************************************
 *                                                                             *
 *                           PUBLIC PLANNER API                                *
 *                                                                             *
 *******************************************************************************/

extern "C" planner_t *planner_new (int64_t base_time, uint64_t duration,
                                   uint64_t resource_totals,
                                   const char *resource_type)
{
    planner_t *ctx = NULL;

    if (duration < 1 || !resource_type) {
        errno = EINVAL;
        goto done;
    } else if (resource_totals > INT64_MAX) {
        errno = ERANGE;
        goto done;
    }

    ctx = new planner_t ();
    ctx->total_resources = (int64_t)resource_totals;
    ctx->resource_type = strdup (resource_type);
    initialize (ctx, base_time, duration);

done:
    return ctx;
}

extern "C" int planner_reset (planner_t *ctx,
                              int64_t base_time, uint64_t duration)
{
    if (!ctx || duration < 1) {
        errno = EINVAL;
        return -1;
    }

    erase (ctx);
    initialize (ctx, base_time, duration);
    return 0;
}

extern "C" void planner_destroy (planner_t **ctx_p)
{
    if (ctx_p && *ctx_p) {
        restore_track_points (*ctx_p);
        erase (*ctx_p);
        if ((*ctx_p)->resource_type)
            free ((*ctx_p)->resource_type);
        free (*ctx_p);
        *ctx_p = NULL;
    }
}

extern "C" int64_t planner_base_time (planner_t *ctx)
{
    if (!ctx) {
        errno = EINVAL;
        return -1;
    }
    return ctx->plan_start;
}

extern "C" int64_t planner_duration (planner_t *ctx)
{
    if (!ctx) {
        errno = EINVAL;
        return -1;
    }
    return ctx->plan_end - ctx->plan_start;
}

extern "C" int64_t planner_resource_total (planner_t *ctx)
{
    if (!ctx) {
        errno = EINVAL;
        return -1;
    }
    return ctx->total_resources;
}

extern "C" const char *planner_resource_type (planner_t *ctx)
{
    if (!ctx) {
        errno = EINVAL;
        return NULL;
    }
    return (const char *)ctx->resource_type;
}

extern "C" int64_t planner_avail_time_first (
                       planner_t *ctx, int64_t on_or_after,
                       uint64_t duration, uint64_t request)
{
    int64_t t = -1;
    if (!ctx || on_or_after < ctx->plan_start
        || on_or_after >= ctx->plan_end || duration < 1) {
        errno = EINVAL;
        return -1;
    }
    if (static_cast<int64_t> (request) > ctx->total_resources) {
        errno = ERANGE;
        return -1;
    }
    restore_track_points (ctx);
    ctx->avail_time_iter_set = 1;
    copy_req (ctx->current_request, on_or_after, duration, request);
    if ( (t = avail_at (ctx, on_or_after, duration, (int64_t)request)) == -1)
        errno = ENOENT;
    return t;
}

extern "C" int64_t planner_avail_time_next (planner_t *ctx)
{
    int64_t t = -1;
    int64_t on_or_after = -1;
    uint64_t duration = 0;
    int64_t request_count = 0;
    if (!ctx || !ctx->avail_time_iter_set) {
        errno = EINVAL;
        return -1;
    }
    request_count = ctx->current_request->count;
    on_or_after = ctx->current_request->on_or_after;
    duration = ctx->current_request->duration;
    if (request_count > ctx->total_resources) {
        errno = ERANGE;
        return -1;
    }
    if ( (t = avail_at (ctx, on_or_after, duration, (int64_t)request_count)) ==
          -1)
        errno = ENOENT;
    return t;
}

extern "C" int planner_avail_during (planner_t *ctx, int64_t start_time,
                                     uint64_t duration, uint64_t request)
{
    bool ok = false;
    if (!ctx || duration < 1) {
        errno = EINVAL;
        return -1;
    }
    if (static_cast<int64_t> (request) > ctx->total_resources) {
        errno = ERANGE;
        return -1;
    }
    ok = avail_during (ctx, start_time, duration, (int64_t)request);
    return ok? 0 : -1;
}

extern "C" int64_t planner_avail_resources_during (planner_t *ctx, int64_t at,
                                                   uint64_t duration)
{
    scheduled_point_t *min_point = NULL;
    if (!ctx || at > ctx->plan_end || duration < 1) {
        errno = EINVAL;
        return -1;
    }
    min_point = avail_resources_during (ctx, at, duration);
    return min_point->remaining;
}

extern "C" int64_t planner_avail_resources_at (planner_t *ctx, int64_t at)
{
    scheduled_point_tree_t *spt = NULL;
    scheduled_point_t *state = NULL;
    if (!ctx || at > ctx->plan_end) {
        errno = EINVAL;
        return -1;
    }
    state = ctx->sched_point_tree.get_state (at);
    return state->remaining;
}

extern "C" int64_t planner_add_span (planner_t *ctx, int64_t start_time,
                                     uint64_t duration, uint64_t request)
{
    span_t *span = NULL;
    zlist_t *list = NULL;
    scheduled_point_t *start_point = NULL;
    scheduled_point_t *last_point = NULL;

    if (!avail_during (ctx, start_time, duration, (int64_t)request)) {
        errno = EINVAL;
        return -1;
    }
    if ( !(span = span_new (ctx, start_time, duration, request)))
        return -1;

    restore_track_points (ctx);
    list = zlist_new ();
    start_point = get_or_new_point (ctx, span->start);
    start_point->ref_count++;
    last_point = get_or_new_point (ctx, span->last);
    last_point->ref_count++;

    fetch_overlap_points (ctx, span->start, duration, list);
    update_points_add_span (ctx, list, span);

    start_point->new_point = 0;
    span->start_p = start_point;
    last_point->new_point = 0;
    span->last_p = last_point;

    update_mintime_resource_tree (ctx, list);

    zlist_destroy (&list);
    span->in_system = 1;
    ctx->avail_time_iter_set = 0;

    return span->span_id;
}

extern "C" int planner_rem_span (planner_t *ctx, int64_t span_id)
{
    char key[32];
    int rc = -1;
    span_t *span = NULL;
    zlist_t *list = NULL;
    uint64_t duration = 0;

    if (!ctx) {
        errno = EINVAL;
        goto done;
    }
    sprintf (key, "%ju", (intmax_t)span_id);
    if ( !(span = static_cast<span_t *> (
                      zhashx_lookup (ctx->span_lookup, key)))) {
        errno = EINVAL;
        goto done;
    }

    restore_track_points (ctx);
    list = zlist_new ();
    duration = span->last - span->start;
    span->start_p->ref_count--;
    span->last_p->ref_count--;
    fetch_overlap_points (ctx, span->start, duration, list);
    update_points_subtract_span (ctx, list, span);
    update_mintime_resource_tree (ctx, list);
    span->in_system = 0;

    if (span->start_p->ref_count == 0) {
        ctx->sched_point_tree.remove (span->start_p);
        if (span->start_p->in_mt_resource_tree)
            ctx->mt_resource_tree.remove (span->start_p);
        free (span->start_p);
        span->start_p = NULL;
    }
    if (span->last_p->ref_count == 0) {
        ctx->sched_point_tree.remove (span->last_p);
        if (span->last_p->in_mt_resource_tree)
            ctx->mt_resource_tree.remove (span->last_p);
        free (span->last_p);
        span->last_p = NULL;
    }
    zhashx_delete (ctx->span_lookup, key);
    zlist_destroy (&list);
    ctx->avail_time_iter_set = 0;
    rc = 0;

done:
    return rc;
}

extern "C" int64_t planner_span_first (planner_t *ctx)
{
    int64_t rc = -1;
    span_t *span = NULL;
    if (!ctx) {
        errno = EINVAL;
        goto done;
    }
    if ( !(span = static_cast<span_t *> (zhashx_first (ctx->span_lookup)))) {
        errno = EINVAL;
        goto done;

    }
    rc = span->span_id;
done:
    return rc;
}

extern "C" int64_t planner_span_next (planner_t *ctx)
{
    int64_t rc = -1;
    span_t *span = NULL;
    if (!ctx) {
        errno = EINVAL;
        goto done;
    }
    if ( !(span = static_cast<span_t *> (zhashx_next (ctx->span_lookup)))) {
        errno = EINVAL;
        goto done;

    }
    rc = span->span_id;
done:
    return rc;
}

extern "C" size_t planner_span_size (planner_t *ctx)
{
    if (!ctx) {
        errno = EINVAL;
        return 0;
    }
    return zhashx_size (ctx->span_lookup);
}


extern "C" bool planner_is_active_span (planner_t *ctx, int64_t span_id)
{
    bool rc = false;
    char key[32];
    span_t *span = NULL;
    if (!ctx) {
        errno = EINVAL;
        goto done;
    }
    sprintf (key, "%ju", (intmax_t)span_id);
    if ( !(span = static_cast<span_t *> (
                      zhashx_lookup (ctx->span_lookup, key)))) {
        errno = EINVAL;
        goto done;
    }
    rc = (span->in_system)? true : false;
done:
    return rc;
}

extern "C" int64_t planner_span_start_time (planner_t *ctx, int64_t span_id)
{
    char key[32];
    int64_t rc = -1;
    span_t *span = NULL;
    if (!ctx) {
        errno = EINVAL;
        goto done;
    }
    sprintf (key, "%ju", (intmax_t)span_id);
    if ( !(span = static_cast<span_t *> (
                      zhashx_lookup (ctx->span_lookup, key)))) {
        errno = EINVAL;
        goto done;
    }
    rc = span->start;
done:
    return rc;
}

extern "C" int64_t planner_span_duration (planner_t *ctx, int64_t span_id)
{
    char key[32];
    int64_t rc = -1;
    span_t *span = NULL;
    if (!ctx) {
        errno = EINVAL;
        goto done;
    }
    sprintf (key, "%ju", (intmax_t)span_id);
    if ( !(span = static_cast<span_t *> (
                      zhashx_lookup (ctx->span_lookup, key)))) {
        errno = EINVAL;
        goto done;
    }
    rc = span->last - span->start;
done:
    return rc;
}

extern "C" int64_t planner_span_resource_count (planner_t *ctx, int64_t span_id)
{
    char key[32];
    int64_t rc = -1;
    span_t *span = NULL;
    if (!ctx) {
        errno = EINVAL;
        goto done;
    }
    sprintf (key, "%ju", (intmax_t)span_id);
    if ( !(span = static_cast <span_t *> (
                      zhashx_lookup (ctx->span_lookup, key)))) {
        errno = EINVAL;
        goto done;
    }
    rc = span->planned;
done:
    return rc;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
