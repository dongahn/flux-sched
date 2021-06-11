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

#include "planner_internal.h"

static int64_t right_branch_mintime (struct rb_node *n)
{
    int64_t min_time = INT64_MAX;
    struct rb_node *right = n->rb_right;
    if (right)
        min_time = rb_entry (right, scheduled_point_t, resource_rb)->subtree_min;

    scheduled_point_t *this_data = rb_entry (n, scheduled_point_t, resource_rb);
    return  (this_data->at < min_time)? this_data->at : min_time;
}

static scheduled_point_t *find_mintime_point (struct rb_node *anchor,
                                              int64_t min_time)
{
    if (!anchor)
        return NULL;

    scheduled_point_t *this_data = NULL;
    this_data = rb_entry (anchor, scheduled_point_t, resource_rb);
    if (this_data->at == min_time)
        return this_data;

    struct rb_node *node = anchor->rb_right;
    while (node) {
        this_data = rb_entry (node, scheduled_point_t, resource_rb);
        if (this_data->at == min_time)
            return this_data;
        if (node->rb_left
            && (rb_entry(node->rb_left, scheduled_point_t,
                        resource_rb)->subtree_min == min_time))
            node = node->rb_left;
        else
            node = node->rb_right;
    }

    // Error condition: when an anchor was found, there must be
    // a point that meets the requirements.
    errno = ENOTSUP;
    return NULL;
}

static int64_t find_mintime_anchor (int64_t request, struct rb_root *mtrt,
                                    struct rb_node **anchor_p)
{
    struct rb_node *node = mtrt->rb_node;
    int64_t min_time = INT64_MAX;
    int64_t right_min_time = INT64_MAX;
    while (node) {
        scheduled_point_t *this_data = NULL;
        this_data = rb_entry (node, scheduled_point_t, resource_rb);
        if (request <= this_data->remaining) {
            // visiting node satisfies the resource requirements. This means all
            // nodes at its subtree also satisfy the requirements. Thus,
            // right_min_time is the best min time.
            right_min_time = right_branch_mintime (node);
            if (right_min_time < min_time) {
                min_time = right_min_time;
                *anchor_p = node;
            }
            // next, we should search the left subtree for potentially better
            // then current min_time;
            node = node->rb_left;
        } else {
            // visiting node does not satisfy the resource requirements. This
            // means that nothing in its left branch will meet these requirements:
            // time to search the right subtree.
            node = node->rb_right;
        }
    }
    return min_time;
}

static int64_t mintime_resource_subtree_min (scheduled_point_t *point)
{
    int64_t min = point->at;
    scheduled_point_t *p = NULL;
    if (point->resource_rb.rb_left) {
        p = rb_entry (point->resource_rb.rb_left, scheduled_point_t, resource_rb);
        if (min > p->subtree_min)
            min = p->subtree_min;
    }
    if (point->resource_rb.rb_right) {
        p = rb_entry (point->resource_rb.rb_right, scheduled_point_t, resource_rb);
        if (min > p->subtree_min)
            min = p->subtree_min;
    }
    return min;
}

static void mintime_resource_propagate (struct rb_node *n, struct rb_node *stop)
{
    int64_t subtree_min;
    while (n != stop) {
        scheduled_point_t *point = rb_entry (n, scheduled_point_t, resource_rb);
        subtree_min = mintime_resource_subtree_min (point);
        if (point->subtree_min == subtree_min)
            break;
        point->subtree_min = subtree_min;
        n = rb_parent (&(point->resource_rb));
    }
}

static void mintime_resource_copy (struct rb_node *src, struct rb_node *dst)
{
    scheduled_point_t *o = rb_entry (src, scheduled_point_t, resource_rb);
    scheduled_point_t *n = rb_entry (dst, scheduled_point_t, resource_rb);
    n->subtree_min = o->subtree_min;
}

static void mintime_resource_rotate (struct rb_node *src, struct rb_node *dst)
{
    scheduled_point_t *o = rb_entry (src, scheduled_point_t, resource_rb);
    scheduled_point_t *n = rb_entry (dst, scheduled_point_t, resource_rb);
    n->subtree_min = o->subtree_min;
    o->subtree_min = mintime_resource_subtree_min (o);
}

static const struct rb_augment_callbacks mintime_resource_aug_cb = {
    mintime_resource_propagate, mintime_resource_copy, mintime_resource_rotate
};



/*******************************************************************************
 *                                                                             *
 *                  Public Minimum Time Resource Tree API                      *
 *                                                                             *
 *******************************************************************************/

void mintime_resource_insert (scheduled_point_t *new_data, mt_resource_tree_t *t)
{
    struct rb_node **link = &(t->root.rb_node);
    scheduled_point_t *this_data = NULL;
    struct rb_node *parent = NULL;
    while (*link) {
        this_data = rb_entry (*link, scheduled_point_t, resource_rb);
        parent = *link;
        if (this_data->subtree_min > new_data->at)
            this_data->subtree_min = new_data->at;
        if (new_data->remaining < this_data->remaining)
            link = &(this_data->resource_rb.rb_left);
        else
            link = &(this_data->resource_rb.rb_right);
    }
    new_data->subtree_min = new_data->at;
    new_data->in_mt_resource_tree = 1;
    rb_link_node (&(new_data->resource_rb), parent, link);
    rb_insert_augmented (&(new_data->resource_rb), &(t->root),
                         &mintime_resource_aug_cb);
}

void mintime_resource_remove (scheduled_point_t *data, mt_resource_tree_t *t)
{
    rb_erase_augmented (&data->resource_rb, &(t->root), &mintime_resource_aug_cb);
    data->in_mt_resource_tree = 0;
}

scheduled_point_t *mintime_resource_mintime (int64_t request, mt_resource_tree_t *t)
{
    struct rb_node *anchor = NULL;
    int64_t min_time = find_mintime_anchor (request, &(t->root), &anchor);
    return find_mintime_point (anchor, min_time);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
