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

#ifndef PLANNER_INTERNAL_TREE_HPP
#define PLANNER_INTERNAL_TREE_HPP

#include "scheduled_point_tree.hpp"
#include "mintime_resource_tree.hpp"

/*! Scheduled point: time at which resource state changes.  Each point's resource
 *  requirements are tracked as a node in a min-time resource (MTR) binary search
 *  tree.
 */
struct scheduled_point_t {
    scheduled_point_rb_node_t point_rb; /* BST node for scheduled point tree */
    mt_resource_rb_node_t resource_rb;  /* BST node for min-time resource tree */
    int64_t at;                 /* Resource-state changing time */
    bool in_mt_resource_tree;   /* 1 when inserted in min-time resource tree */
    bool new_point;             /* 1 when this point is newly created */
    int ref_count;              /* reference counter */
    int64_t scheduled;          /* scheduled quantity at this point */
    int64_t remaining;          /* remaining resources (available) */
};

#endif // PLANNER_INTERNAL_TREE_HPP

/*
 * vi: ts=4 sw=4 expandtab
 */
