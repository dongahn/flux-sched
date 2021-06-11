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

static scheduled_point_t *recent_state (scheduled_point_t *new_data,
                                        scheduled_point_t *old_data)
{
    if (!old_data)
        return new_data;
    return (new_data->at > old_data->at)? new_data : old_data;
}


/*******************************************************************************
 *                                                                             *
 *                 Public Scheduled Point Search Tree API                      *
 *                                                                             *
 *******************************************************************************/

/*******************************************************************************
 *                                                                             *
 *    Scheduled Points Binary Search Tree: O(log n) Scheduled Points Search    *
 *                                                                             *
 *******************************************************************************/

scheduled_point_t *scheduled_point_next (scheduled_point_t *point)
{
    struct rb_node *n = rb_next (&(point->point_rb));
    return rb_entry (n, scheduled_point_t, point_rb);
}

scheduled_point_t *scheduled_point_search (int64_t tm, sched_point_tree_t *t)
{
    struct rb_node *node = t->root.rb_node;
    while (node) {
        scheduled_point_t *this_data = NULL;
        this_data = container_of (node, scheduled_point_t, point_rb);
        int64_t result = tm - this_data->at;
        if (result < 0)
            node = node->rb_left;
        else if (result > 0)
            node = node->rb_right;
        else
            return this_data;
    }
    return NULL;
}

/*! While scheduled_point_search returns the exact match scheduled_point_state
 *  returns the most recent scheduled point, representing the accurate resource
 *  state at the time t.
 */
scheduled_point_t *scheduled_point_state (int64_t at, sched_point_tree_t *t)
{
    scheduled_point_t *last_state = NULL;
    struct rb_node *node = t->root.rb_node;
    while (node) {
        scheduled_point_t *this_data = NULL;
        this_data = container_of (node, scheduled_point_t, point_rb);
        int64_t result = at - this_data->at;
        if (result < 0) {
            node = node->rb_left;
        } else if (result > 0) {
            last_state = recent_state (this_data, last_state);
            node = node->rb_right;
        } else {
            return this_data;
        }
    }
    return last_state;
}

int scheduled_point_insert (scheduled_point_t *new_data, sched_point_tree_t *t)
{
    struct rb_node **link = &(t->root.rb_node);
    struct rb_node *parent = NULL;
    while (*link) {
        scheduled_point_t *this_data = NULL;
        this_data  = container_of (*link, scheduled_point_t, point_rb);
        int64_t result = new_data->at - this_data->at;
        parent = *link;
        if (result < 0)
            link = &((*link)->rb_left);
        else if (result > 0)
            link = &((*link)->rb_right);
        else
            return -1;
    }
    rb_link_node (&(new_data->point_rb), parent, link);
    rb_insert_color (&(new_data->point_rb), &(t->root));
    return 0;
}

int scheduled_point_remove (scheduled_point_t *data, sched_point_tree_t *t)
{
    int rc = -1;
    scheduled_point_t *n = scheduled_point_search (data->at, t);
    if (n) {
        rb_erase (&(n->point_rb), &(t->root));
        // Note: this must only remove the node from the scheduled point tree:
        // DO NOT free memory allocated to the node
        rc = 0;
    }
    return rc;
}

void __scheduled_points_destroy (struct rb_node *node)
{
    if (node->rb_left)
        __scheduled_points_destroy (node->rb_left);
    if (node->rb_right)
        __scheduled_points_destroy (node->rb_right);
    scheduled_point_t *data = container_of (node, scheduled_point_t, point_rb);
    free (data);
}

void scheduled_points_destroy (sched_point_tree_t *t)
{
    __scheduled_points_destroy (t->root.rb_node);
}


/*
 * vi: ts=4 sw=4 expandtab
 */
