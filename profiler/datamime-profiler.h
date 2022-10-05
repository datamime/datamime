/** $lic$
 * Copyright (C) 2021-2022 by Massachusetts Institute of Technology
 *
 * This file is part of Datamime.
 *
 * This tool is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 3.
 *
 * If you use this software in your research, we request that you reference
 * the Datamime paper ("Datamime: Generating Representative Benchmarks by
 * Automatically Synthesizing Datasets", Lee and Sanchez, MICRO-55, October 2022)
 * as the source in any publications that use this software, and that you send
 * us a citation of your work.
 *
 * This tool is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <signal.h>

extern "C" {
#include "perf_util.h"
}

constexpr size_t MAX_GROUP_EVENTS = 6;
constexpr size_t PAGE_SIZE = 4096;
constexpr size_t BUFFER_PAGES = 1;
constexpr int SIGTHYME = 37;
constexpr uint32_t PHASES_BETWEEN_SWITCHES = 10;

template <typename T, size_t N>
constexpr size_t array_size(T (&)[N]) {
    return N;
}

struct ThymeArgs {
    const char* events;
    const char* glob_outfile_name;
    const char* results_dir;
    uint64_t phase_len;
    uint64_t num_phases;
    uint64_t mrc_warmup_period;
    uint64_t mrc_profile_period;
    int tgid;
    volatile bool mrc_est_mode = false;
    volatile bool debug = false;
    std::vector<int> profiled_tids;
};

struct ThymeState {
    int first_finished_thread = -1;
    volatile bool done;
    int cache_line_size;
    int num_logical_cores; // i.e., number of SMT threads
    int cache_num_ways; //TODO: detect programatically
    std::vector<int> assignable_cores;
    // Number of historical profiling samples to average for estimating IPC-curves
    // and MRC-curves
    int HIST_WINDOW_LENGTH = 3;
};

struct EventGroup {
    int tid;
    FILE *outfile;
    int fd; //[hrlee] Seems to be fd of group leader?
    int num_events = 0;
    perf_event_desc_t fds[MAX_GROUP_EVENTS];
    EventGroup(perf_event_desc_t* clock_event_fd,
               perf_event_desc_t* permanent_event_fds,
               int profile_tid, FILE *profile_outfile,
               int num_permanent_events);
    bool add_event(perf_event_desc_t* event_fd);
    void finalize_events();
};

class PerfEvents {
public:
    PerfEvents();
    void create_event_groups();
    void signal_handler(siginfo_t* info, int tid);

private:

    std::unordered_map<int,EventGroup*> event_groups;

    perf_event_desc_t* clock_event_fd;
    constexpr static const char* CLOCK_EVENT = "UNHALTED_REFERENCE_CYCLES";

    perf_event_desc_t* permanent_event_fds;
    int num_permanent_events;
    constexpr static const char* PERMANENT_EVENTS = "INST_RETIRED,"
                                                    "CPU_CLK_UNHALTED";

    perf_event_desc_t* rotating_event_fds;
    int num_rotating_events;


    std::string filter_events(const char* events);

};

