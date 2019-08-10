#include <sys/time.h>
#include <getopt.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <map>
#include <random>
#include <cmath>
#include "planner.h"

#define OPTIONS "L:d:D:w:q:r:s:"
static const struct option longopts[] = {
    {"load",                           required_argument,  0, 'L'},
    {"--load-duration_granule",        required_argument,  0, 'd'},
    {"--query-duration_gran-query",    required_argument,  0, 'D'},
    {"--walltime-max",                 required_argument,  0, 'w'},
    {"--query-count",                  required_argument,  0, 'q'},
    {"--query-repeat",                 required_argument,  0, 'r'},
    {"--stride",                       required_argument,  0, 's'},
    { 0, 0, 0, 0 },
};

struct perf_t {
    double min;
    double max;
    double avg;
};

struct experiment_t {
    perf_t sat_at;
    perf_t sat_during;
    perf_t earliest_at;
    perf_t earliest_during;
};

void print_hist (std::vector<uint64_t> &v)
{
    int i = 0;
    std::map<int, int> hist{};
    for (i = 0; i < v.size (); i++)
        ++hist[v[i]];
    for (auto p : hist) {
        std::cout << std::setw(2)
                  << p.first << ": " << p.second << '\n';
    }
}

int req_mixture_normal_duration_mixture_uniform (int req_mean, int req_std, int req_max,
                                        uint64_t dur_max, int dur_gran, int req_cnt,
                                        std::vector<uint64_t> &reqs,
                                        std::vector<uint64_t> &durs)
{
    int i{};
    std::random_device rd{};
    std::mt19937 gen{rd()};

    std::normal_distribution<> size_dist{(double)req_mean, (double)req_std};
    std::uniform_int_distribution<uint64_t> tm_dist (1, dur_max);
    std::uniform_int_distribution<int> df_flip (0, 1);

    for (i = 0; i < req_cnt; i++) {
        if (df_flip (rd)) {
            reqs.push_back (1);
        } else {
            reqs.push_back (std::round (size_dist (gen)));
        }
        if (df_flip (rd)) {
            durs.push_back (dur_max * dur_gran);
        } else {
            durs.push_back (tm_dist (rd) * dur_gran);
        }

        if (reqs[i] <= 0)
            reqs[i] = 1;
        else if (reqs[i] > req_max)
            reqs[i] = req_max;

        if (durs[i] <= dur_gran)
            durs[i] = dur_gran;
        else if (durs[i] > dur_max * dur_gran)
            durs[i] = dur_max *dur_gran;
    }
    return 0;
}

int req_normal_duration_uniform (int req_mean, int req_std, int req_max,
                                 uint64_t dur_max, int dur_gran, int req_cnt,
                                 std::vector<uint64_t> &reqs,
                                 std::vector<uint64_t> &durs)
{
    int i{};
    std::random_device rd{};
    std::mt19937 gen{rd()};

    std::normal_distribution<> size_dist{(double)req_mean, (double)req_std};
    std::uniform_int_distribution<uint64_t> tm_dist (1, dur_max);

    for (i = 0; i < req_cnt; i++) {
        reqs.push_back (std::round (size_dist (gen)));
        durs.push_back (tm_dist (rd) * dur_gran);

        if (reqs[i] <= 0)
            reqs[i] = 1;
        else if (reqs[i] > req_max)
            reqs[i] = req_max;
    }
    return 0;
}

int req_pow_duration_uniform (int base, int factor_max, uint64_t time_max,
                              int dur_max, int dur_gran,
                              int req_cnt, std::vector<uint64_t> &reqs,
                              std::vector<uint64_t> &times,
                              std::vector<uint64_t> &durs)
{
    int i{};
    std::random_device rd{};
    std::uniform_int_distribution<int> req_dist (0, factor_max);
    std::uniform_int_distribution<uint64_t> tm_dist (1, time_max);
    std::uniform_int_distribution<uint64_t> dur_dist (1, dur_max);

    for (i = 0; i < req_cnt; i++) {
        reqs.push_back ((uint64_t)pow (base, req_dist (rd)) + 0.5);
        times.push_back (tm_dist (rd));
        durs.push_back (dur_dist (rd) * dur_gran);
    }
    return 0;
}

int req_pow_duration_1sec (int base, int factor_max,
                           int req_cnt, std::vector<uint64_t> &reqs,
                           std::vector<uint64_t> &durs)
{
    int i{};
    std::random_device rd{};
    std::uniform_int_distribution<int> req_dist (0, factor_max);

    for (i = 0; i < req_cnt; i++) {
        reqs.push_back ((uint64_t)pow (base, req_dist (rd)) + 0.5);
        durs.push_back (1);
    }
    return 0;
}


int prepopulate_scheduled_points (planner_t *planner, unsigned req_cnt,
                                  unsigned req_mean, unsigned req_std,
                                  unsigned req_max, unsigned dur_max,
                                  unsigned dur_gran)
{
    int i = 0;
    uint64_t plan_end = 0;
    std::vector<uint64_t> reqs;
    std::vector<uint64_t> durs;
    std::vector<uint64_t> times;

    if (!planner) {
        std::cout << "[Error] Args for " << __FUNCTION__ << std::endl;
        return -1;
    }

    req_normal_duration_uniform (req_mean, req_std, req_max, dur_max,
                                 dur_gran, req_cnt, reqs, durs);
#if 0
    //req_mixture_normal_duration_mixture_uniform (req_mean, req_std, req_max, dur_max,
    //                               dur_gran, req_cnt, reqs, durs);
    //req_pow_duration_uniform (2, 7, 1000,
    //                          dur_max, dur_gran, req_cnt, reqs, times, durs);

    std::cout << "requests====================" << std::endl;
    print_hist (reqs);
    std::cout << "durs====================" << std::endl;
    print_hist (durs);
#endif

    for (i = 0; i < req_cnt; i++) {
        int64_t t = planner_avail_time_first (planner, 0, durs[i], reqs[i]);
        planner_add_span (planner, t, durs[i], reqs[i]);
        plan_end = (plan_end < (t + durs[i]))? (t + durs[i]) : plan_end;
#if 0
        if (i % 100 == 0) {
            printf ("\r%.2f", (double)i/(double)req_cnt * 100.0f);
        }
#endif
    }
    //std::cout << std::endl;
    return plan_end / dur_gran;
}

int log_perf (double min, double max, double avg, perf_t &perf)
{
    perf.min = min;
    perf.max = max;
    perf.avg = avg;
}

int performance_sat_at (planner_t *planner,
                        std::vector<uint64_t> &reqs,
                        std::vector<uint64_t> &times,
                        perf_t &perf)
{
    int i = 0;
    int nyes = 0;
    int64_t rc;
    double min = 9999999.9f, max = 0.0f, accm = 0.0f, elapse = 0.0f;
    struct timeval begin, end;

    if (!planner || reqs.empty () || times.empty () || reqs.size () != times.size ()) {
        std::cout << "[Error] Args for " << __FUNCTION__ << std::endl;
        return -1;
    }

    for (i = 0; i < reqs.size (); i++) {
        gettimeofday (&begin, NULL);
        if ((rc = planner_avail_resources_at (planner, times[i])) < 0) {
            std::cout << "[Error] " << "planner_avail_resources_at" << std::endl;
            return -1;
        }
        gettimeofday (&end, NULL);
        nyes = (reqs[i] <= rc)? nyes + 1 : nyes;
        elapse = ((double)end.tv_sec + (double)end.tv_usec/1000000.0)
                 - ((double)begin.tv_sec + (double)begin.tv_usec/1000000.0);
        min = (elapse < min)? elapse : min;
        max = (elapse > max)? elapse : max;
        accm += elapse;
    }

    log_perf (min, max, accm / (double)reqs.size (), perf);
    return nyes;
}

int performance_sat_during (planner_t *planner,
                            std::vector<uint64_t> &reqs,
                            std::vector<uint64_t> &times,
                            std::vector<uint64_t> &durs, perf_t &perf)
{
    int i = 0;
    int nyes = 0;
    int64_t rc;
    double min = 9999999.9f, max = 0.0f, accm = 0.0f, elapse = 0.0f;
    struct timeval begin, end;

    if (!planner || reqs.empty () || times.empty () || reqs.size () != times.size ()) {
        std::cout << "[Error] Args for " << __FUNCTION__ << std::endl;
        return -1;
    }

    for (i = 0; i < reqs.size (); i++) {
        gettimeofday (&begin, NULL);
        if ((rc = planner_avail_resources_during (planner, times[i], durs[i])) < 0) {
            std::cout << "[Error] " << "planner_avail_resources_during" << std::endl;
            return -1;
        }
        gettimeofday (&end, NULL);
        nyes = (reqs[i] <= rc)? nyes + 1 : nyes;
        elapse = ((double)end.tv_sec + (double)end.tv_usec/1000000.0)
                 - ((double)begin.tv_sec + (double)begin.tv_usec/1000000.0);
        min = (elapse < min)? elapse : min;
        max = (elapse > max)? elapse : max;
        accm += elapse;
    }

    log_perf (min, max, accm / (double)reqs.size (), perf);
    return nyes;

}

int performance_earliest_fit (planner_t *planner,
                              std::vector<uint64_t> &reqs,
                              std::vector<uint64_t> &durs,
                              perf_t &perf)
{
    int i = 0;
    int64_t rc;
    double min = 9999999.9f, max = 0.0f, accm = 0.0f, elapse = 0.0f;
    struct timeval begin, end;

    if (!planner || reqs.empty () || durs.empty () || reqs.size () != durs.size ()) {
        std::cout << "[Error] Args for " << __FUNCTION__ << std::endl;
        return -1;
    }

    for (i = 0; i < reqs.size (); i++) {
        gettimeofday (&begin, NULL);
        if ((rc = planner_avail_time_first (planner, 0, durs[i], reqs[i])) < 0) {
            std::cout << "[Error] " << "planner_avail_time_first" << std::endl;
            return -1;
        }
        gettimeofday (&end, NULL);
        elapse = ((double)end.tv_sec + (double)end.tv_usec/1000000.0)
                 - ((double)begin.tv_sec + (double)begin.tv_usec/1000000.0);
        min = (elapse < min)? elapse : min;
        max = (elapse > max)? elapse : max;
        accm += elapse;
    }

    log_perf (min, max, accm / (double)reqs.size (), perf);
    return 0;
}

int run_one_experiment (int n_pjobs, int n_queries, int p_dur_gran,
                        int q_dur_gran, int walltime_max, experiment_t &e)
{
    int {};
    int64_t scaled_tm = 0;
    const char *n = "a_resource_pool";
    std::vector<uint64_t> reqs, reqs1, reqs2, reqs3;
    std::vector<uint64_t> times, times1, times3;
    std::vector<uint64_t> durs, durs1, durs2, durs3;

    planner_t *p = planner_new (0, INT64_MAX, 128, n);

    /*
     * 0) Create loads
     */
    if ((scaled_tm = prepopulate_scheduled_points (p, n_pjobs, 64, 16, 128,
                                                   walltime_max,
                                                   p_dur_gran)) < 0) {
        return -1;
    }

    /*
     * 1) performance_sat_at_instant_tm
     */
    std::cout << "[Performance_sat_at <-- pow2 Req + Instant time]" << std::endl;
    req_pow_duration_uniform (2, 7, scaled_tm, walltime_max, q_dur_gran,
                              n_queries, reqs, times, durs);
    int nyes = performance_sat_at (p, reqs, times, e.sat_at);
    std::cout << "Num of yes: " << nyes << std::endl;
    std::cout << "Num of no: " << n_queries - nyes << std::endl;
    std::cout << "Done!" << std::endl << std::endl;


    /*
     * 2) performance_sat_during
     */
    std::cout << "[Performance_sat_duration <-- pow2 Req + Uniform duration]" << std::endl;
    req_pow_duration_uniform (2, 7, scaled_tm, walltime_max, q_dur_gran,
                              n_queries, reqs1, times1, durs1);
    nyes = performance_sat_during (p, reqs1, times1, durs1, e.sat_during);
    std::cout << "Num of yes: " << nyes << std::endl;
    std::cout << "Num of no: " << n_queries - nyes << std::endl;
    std::cout << "Done!" << std::endl << std::endl;


    /*
     * 3) performance_earliest_fit
     */
    std::cout << "[performance_earliest_fit <-- pow2 Req + Unit duration]" << std::endl;
    req_pow_duration_1sec (2, 7, 1000000, reqs2, durs2);
    performance_earliest_fit (p, reqs2, durs2, e.earliest_at);
    std::cout << "Done!" << std::endl << std::endl;


    /*
     * 4) performance_earliest_fit
     */
    std::cout << "[performance_earliest_fit <-- pow2 Req + Uniform duration]" << std::endl;
    req_pow_duration_uniform (2, 7, scaled_tm, walltime_max, q_dur_gran,
                              n_queries, reqs3, times3, durs3);
    performance_earliest_fit (p, reqs3, durs3, e.earliest_during);
    std::cout << "Done!" << std::endl << std::endl;

    planner_destroy (&p);
    return 0;
}

void do_format (std::map<int, std::vector<experiment_t>> &imap)
{
    std::cout << std::setw(20) << "Load"
             << std::setw(20) << "SatAt"
             << std::setw(20) << "SatDuring"
             << std::setw(20) << "EarliestAt"
             << std::setw(20) << "SatDuring" << std::endl;

    for (auto &l : imap) {
        double best_sat_at = 999999999.9;
        double best_sat_during = 999999999.9;
        double best_earliest_at = 999999999.9;
        double best_earliest_during = 999999999.9;
        for (auto p : l.second) {
            if (best_sat_at > p.sat_at.avg) best_sat_at = p.sat_at.avg;
            if (best_sat_during > p.sat_during.avg) best_sat_during = p.sat_during.avg;
            if (best_earliest_at > p.earliest_at.avg) best_earliest_at = p.earliest_at.avg;
            if (best_earliest_during > p.earliest_during.avg) best_earliest_during = p.earliest_during.avg;
        }
        std::cout << std::fixed;
        //std::cout << std::setw(15) << l.first
        //          << std::setw(15) << std::fixed << std::setprecision(10) << best_sat_at * 1000000.0
        //          << std::setw(15) << best_sat_during * 1000000.0
        //          << std::setw(15) << best_earliest_at * 1000000.0
        //          << std::setw(15) << best_earliest_during * 1000000.0 << std::endl;
        std::cout << std::setw(20) << l.first
                  << std::setw(20) << std::setprecision(11) << best_sat_at * 1000000.0
                  << std::setw(20) << std::setprecision(11) << best_sat_during * 1000000.0
                  << std::setw(20) << std::setprecision(11) << best_earliest_at * 1000000.0
                  << std::setw(20) << std::setprecision(11) << best_earliest_during * 1000000.0 << std::endl;
    }
}

int main (int argc, char *argv[])
{
    int n_pjobs = 100000;
    int n_queries = 1000000;
    int p_dur_gran = 1800;
    int q_dur_gran = 1800;
    int walltime_max = 24;
    int n_repeats = 1;
    int stride = 10;
    int ch = 0;
    int i, j;
    std::map<int, std::vector<experiment_t>> imap;

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
        case 'L': // --load
            n_pjobs = atoi (optarg);
            break;
        case 'd': // --load-duration_granule
            p_dur_gran = atoi (optarg);
            break;
        case 'D': // --query-duration_gran-query
            q_dur_gran = atoi (optarg);
            break;
        case 'w': // --walltime-max
            walltime_max = atoi (optarg);
            break;
        case 'q': // --query-count
            n_queries = atoi (optarg);
            break;
        case 'r': // --n_repeats
            n_repeats = atoi (optarg);
            break;
        case 's': // --stride
            stride = atoi (optarg);
            break;
        default:
            break;
            exit (1);
        }
    }

    if (optind != argc) {
        std::cerr << "[Err] Incorrect commandline" << std::endl;
        exit (1);
    }

    for (i = 1; i <= n_pjobs; i *= stride) {
        std::cout << "********************* Starting Load " << i << " *********************" << std::endl << std::endl;
        std::vector<experiment_t> expV;
        for (j = 0; j < n_repeats; j++) {
            experiment_t e;
            std::cout << "--------------------- Iter " << j << "------------------------" << std::endl << std::endl;
            if (run_one_experiment (i, n_queries, p_dur_gran,
                                    q_dur_gran, walltime_max, e) < 0) {
                std::cerr << "[Err] run_one_experiment failed" << std::endl;
            }
            expV.push_back (e);
        }
        imap[i] = expV;
    }

    do_format (imap);

    return EXIT_SUCCESS;
}
