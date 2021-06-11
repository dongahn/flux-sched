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

#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <cstdlib>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <vector>
#include <map>
#include <iostream>
#include <sys/time.h>
#include "planner.h"
#include "src/common/libtap/tap.h"

static double get_elapse_time (timeval &st, timeval &et)
{
    double ts1 = (double)st.tv_sec + (double)st.tv_usec/1000000.0f;
    double ts2 = (double)et.tv_sec + (double)et.tv_usec/1000000.0f;
    return ts2 - ts1;
}

static void to_stream (int64_t base_time, uint64_t duration, uint64_t cnts,
                      const char *type, std::stringstream &ss)
{
    if (base_time != -1)
        ss << "B(" << base_time << "):";
    ss << "D(" << duration << "):" << "R_";
    ss << type << "(" << cnts << ")";
}

int query_resource_4spans_overlap (planner_t *ctx)
{
    int i = 0;
    int repeat = 0;
    int64_t t = -1;
    bool bo = true;

    for (repeat = 0; repeat < 10; repeat++) {
        for (i = 0; i < 1000000; i += 4) {
            int j = 0;
            for (t = planner_avail_time_first (ctx, 0, 3, static_cast<uint64_t> (i+1));
            t != -1 && j < 4;
            t = planner_avail_time_next (ctx)) {
                bo = (bo && t == (4*i + j*4));
                j++;
            }
        }
    }
    ok (bo && !errno, "planner_avail_time 40 million times");
    return 0;
}

int query_time_4spans_overlap (planner_t *ctx)
{
    int i = 0;
    int repeat = 0;
    int rc = 0;
    bool bo = false;

    for (repeat = 0; repeat < 10; repeat++) {
        for (i = 0; i < 4000000; i += 4) {
            rc = planner_avail_during (ctx, 4*i, 4, 1);
            bo = (bo || rc != 0);
        }
    }
    ok (!bo && !errno, "planner_avail_during 40 million times");
    return 0;
}

int stress_4spans_overlap (planner_t *ctx)
{
    int i = 0;
    int64_t span;
    bool bo = false;

    for (i = 0; i < 4000000; ++i) {
	span = planner_add_span (ctx, 4*i, 4, static_cast<uint64_t> (3999999 - i));
        bo = (bo || span == -1);
    }
    ok (!bo && !errno, "add 4 million spans");

    return 0;
}

int run_and_time_function (int (*fptr)(planner_t *),
                            planner_t *ctx, const std::string &label)
{
    int rc;
    struct timeval start, end;

    gettimeofday (&start, NULL);
    rc = fptr (ctx);
    gettimeofday (&end, NULL);

    std::cout << "Elapse Time (" << label << ") "
	      << get_elapse_time (start, end) << " seconds" << std::endl;
    return rc;
}



int main (int argc, char *argv[])
{
    int (*func) (planner_t *);

    plan (4);

    uint64_t resource_total = 4000000;
    char resource_type[] = "hardware-thread";
    planner_t *ctx = NULL;
    std::stringstream ss;

    errno = 0;
    to_stream (0, INT64_MAX, resource_total, resource_type, ss);
    ctx = planner_new (0, INT64_MAX, resource_total, resource_type);
    ok ((ctx && !errno), "new with (%s)", ss.str ().c_str ());

    // 1) Span Add Performance
    func = stress_4spans_overlap;
    run_and_time_function (func, ctx, "stress_4spans_overlap");

    // 2) Scheduled Points Search Performance
    func = query_time_4spans_overlap;
    run_and_time_function (func, ctx, "query_time_4spans_overlap");

    // 3) Mintime Resource Search Performance
    func = query_resource_4spans_overlap;
    run_and_time_function (func, ctx, "query_resource_4spans_overlap");

    planner_destroy (&ctx);

    done_testing ();

    return EXIT_SUCCESS;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
