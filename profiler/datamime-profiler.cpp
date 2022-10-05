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
#include "datamime-profiler.h"
#include <string>
#include <iostream>
#include <cassert>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sstream>
#include <pthread.h>
#include <signal.h>
#include <sched.h>
#include <errno.h>
#include <numa.h>
#include <mutex>
#include <atomic>
#include "cmt.h"
#include "cache_utils.h"
#include "mrc.h"
#include "cat.h"
#include "easylogging++.h"

using namespace cache_utils;
#define gettid() syscall(SYS_gettid)

// Enable logger thread safety
#define ELPP_THREAD_SAFE

// [FIXME] (hrlee) PTRACE_EVENT_STOP is necessary to properly handle group stops,
// but this is not provided until glibc v2.26. Since we know the value of
// this ptrace event code, manually add it if not defined.
// This is an ugly hack...
#ifndef PTRACE_EVENT_STOP
#define PTRACE_EVENT_STOP 128
#endif

/////////////////////////////////////////////////////////////////////////
///// Additional global variables for miss curve monitoring
////////////////////////////////////////////////////////////////////////

bool monitorStartFlag(false);
int monitorLen = 1; //estimate MRC IPC point every monitorLen phases
bool firstMRCInvocation(true);
int sampleSlicesIdx = -1;
int numSamples = 0;
int num_ways_to_sample = 7;

uint64_t mrc_warmup_interval;
uint64_t mrc_profile_interval;
uint64_t mrc_invoke_monitor_len;

arma::cube allAppsCacheAssignments;
// (row,col) = How many ways we are sampling <row> thread in <col> COS.
arma::mat currentlySampling;
arma::mat sampledMRCs;
// First column is how many ways each core is allocated
// Second column is whether sampling has finished for the given CORE
// 0 == finished, 1 == still sampling
arma::mat sampledIPCs;
arma::vec loggingMRCFlags;

struct ThreadInfo {
    // Thread metadata
    int tidx;
    int tid;
    int tgid;
    std::vector<int> cores; // Set of CPUs the thread is eligible to run on.
    int64_t phases;
    int64_t phases_with_current_group;
    bool is_dummy_thread = false;

    //arma::vec xPoints = arma::linspace<arma::vec>(0, 0, num_ways_to_sample);
    //arma::vec yPoints_ipc = arma::linspace<arma::vec>(0, 0, num_ways_to_sample);
    //arma::vec yPoints_mpki = arma::linspace<arma::vec>(0, 0, num_ways_to_sample);
    arma::vec xPoints;
    arma::vec yPoints_ipc;
    arma::vec yPoints_mpki;

    arma::vec mrcEstAvg;
    arma::mat mrcEstimates;

    arma::vec ipcCurveAvg;
    arma::mat ipcCurveEstimates;

    int mrc_est_index;
    int p_sample_slices_idx;

    // Logfiles
    // outfile = PMU performance counters + RMID counters with Intel CMT
    // mrc_outfile = MRC estimates
    // ipc_outfile = IPC estimates
    FILE* outfile;
    FILE* mrc_outfile;
    FILE* ipc_outfile;

    int rmid; // Resource monitoring ID. Each logical processor is associated with
              // an RMID.
              // See Chapter 17.18 and 17.19 of Intel 64 and IA-32 Architectures
              // Software Developer's Manual, Volume 3

    // Required performance counters saved from current phase
    // (FIXME) hrlee: This is a very terrible name choice from kpart btw. Need to fix.
    std::vector<uint64_t> values;

    // Counters used for miss curve sampling
    uint64_t lastInstrCtr;
    uint64_t lastCyclesCtr;
    uint64_t lastMemTrafficCtr;

    int64_t memTrafficLast;
    int64_t memTrafficTotal;
    int64_t avgCacheOccupancy;

    // Set of rotating event groups.
    std::unordered_map<int,EventGroup*> event_groups;

    perf_event_desc_t* clock_event_fd;
    int num_clock_events;
    constexpr static const char* CLOCK_EVENT = "UNHALTED_REFERENCE_CYCLES";

    perf_event_desc_t* permanent_event_fds;
    int num_permanent_events;
    constexpr static const char* PERMANENT_EVENTS = "INST_RETIRED,"
                                                    "CPU_CLK_UNHALTED";

    perf_event_desc_t* rotating_event_fds;
    int num_rotating_events;

    // Member functions
    void create_event_groups();
    void flush();

    ~ThreadInfo() {
        fflush(outfile);
        fflush(mrc_outfile);
        fflush(ipc_outfile);
        fclose(outfile);
        fclose(mrc_outfile);
        fclose(ipc_outfile);
    }

    std::string filter_events(const char* events);
};


// Thread Id -> ThreadInfo mapping
std::unordered_map<int, ThreadInfo*> tid_map;
// Perf File Descriptor -> Thread Id mapping
std::unordered_map<int, int> fd_map;

// CMT-relevant variables.
CMTController cmtCtrl;
std::string lmbName = "LOCAL_MEM_TRAFFIC";
std::string l3OccupName = "L3_OCCUPANCY";

///////////////////////////////////////////////////////////////////

// Globals
ThymeArgs args;
ThymeState state;

int num_profiled_threads = 0;
int thr_idx_profiled_global = 0; // start with thread 0, up to (computed)
                                  // num_profiled_threads

bool enable_array_scans;
std::mutex enable_array_scans_m;
std::atomic<int> num_scan_threads{0};
std::atomic<int> scan_thread_tid;

struct Elem {
    int32_t val;
} __attribute__ ((aligned (4)));

// The dummy thread will execute this function, scanning a very large array
// such that it will quickly fill up the shared ways.
void *scan_array(void *arg) {
    int tidx = *((int *) arg);
    int tid = gettid();
    scan_thread_tid = tid;
    LOG(INFO) << "[DATAMIME-PROFILER] Dummy thread"
        << tidx
        << " (tid "
        << tid
        << ") started";

    // Important! Posix threads inherit signal masks from the spawning process.
    // Correct solution is to have the dummy thread ignore all signals.
    // It doesn't matter if set this up a bit late here since the parent
    // will launch dummy thread(s) before setting up SIGTHYME and associated
    // handler.
    sigset_t st_set;
    sigfillset(&st_set);
    int s = pthread_sigmask(SIG_BLOCK, &st_set, NULL);
    el::Logger* logger = el::Loggers::getLogger("default");
    if (s != 0) {
        logger->fatal("[DATAMIME-PROFILER] Could not block all signals for dummy thread %d\n", tid);
        std::exit(1);
    }

    // Atomically update number of of threads
    num_scan_threads++;

    size_t arrayElems = 32000 * 1024ul / sizeof(Elem); // traverse 32MB array
    volatile Elem* array = new volatile Elem[arrayElems];
    while (1) {
        enable_array_scans_m.lock();
        if (enable_array_scans) {
            enable_array_scans_m.unlock();
            for (int64_t i = 0; i < arrayElems; i++) {
                array[i].val /= 5;
            }
        }
        else
            enable_array_scans_m.unlock();
    }
    return 0;
}


// -------------- Helper Functions  --------------------- //

void block_sigthyme() {
    sigset_t sigthyme_mask;
    sigemptyset(&sigthyme_mask);
    auto ret = sigaddset(&sigthyme_mask, SIGTHYME);
    assert(!ret);
    ret = sigprocmask(SIG_BLOCK, &sigthyme_mask, nullptr);
    assert(!ret);
}

void unblock_sigthyme() {
    sigset_t sigthyme_mask;
    sigaddset(&sigthyme_mask, SIGTHYME);
    auto ret = sigprocmask(SIG_UNBLOCK, &sigthyme_mask, nullptr);
    assert(!ret);
}

// Returns mem traffic of this phase for an rmid
// memTrafficLast should be cumulative mem traffic up to (but excluding) this phase.
// Special handling for rmid0 since last mem traffic for that is stored globally.
int64_t getMemTrafficDelta(int rmid, int64_t memTrafficLast) {
    int64_t delta = 0;
    int64_t memTraffic = cmtCtrl.getLocalMemTraffic(rmid);

    // Detect overflow
    if (memTraffic < memTrafficLast) {
        LOG(INFO) << "[datamime-profiler] MBM counter overflow detected.";
        delta += (cmtCtrl.getMemTrafficMax() - memTrafficLast);
        delta += memTraffic;
    } else {
        delta += (memTraffic - memTrafficLast);
    }

    return delta;
}


void updateMemTraffic(ThreadInfo &tinfo) {
    tinfo.memTrafficTotal += getMemTrafficDelta(tinfo.rmid, tinfo.memTrafficLast);
    tinfo.memTrafficLast = cmtCtrl.getLocalMemTraffic(tinfo.rmid);
}

void updateCacheOccupancy(ThreadInfo &tinfo) {
  tinfo.avgCacheOccupancy = cmtCtrl.getLlcOccupancy(tinfo.rmid);
}

void initCmt(ThreadInfo &tinfo) {
  for (int c : tinfo.cores) {
    cmtCtrl.setRmid(c, tinfo.rmid);
  }

  tinfo.memTrafficLast = cmtCtrl.getLocalMemTraffic(tinfo.rmid);
  tinfo.memTrafficTotal = 0;
  tinfo.avgCacheOccupancy = 0;
}

void dump_mrc_estimates(ThreadInfo &tinfo) {
  rewind(tinfo.mrc_outfile);

  // Smoothen before dumping:
  double prevValue = 0.0;
  for (int j = 0; j < tinfo.mrc_est_index + 1; j++) {
    for (int i = 1; i < tinfo.mrcEstimates.n_rows; i++) {
      prevValue = tinfo.mrcEstimates(i - 1, j);
      tinfo.mrcEstimates(i, j) = std::min(prevValue, tinfo.mrcEstimates(i, j));
    }
  }

  // dump to output file of online samples for this thread
  for (int i = 0; i < tinfo.mrcEstimates.n_rows; i++) {
    for (int j = 0; j < tinfo.mrc_est_index + 1; j++) {
      fprintf(tinfo.mrc_outfile, "%f ", tinfo.mrcEstimates(i, j));
    }
    fprintf(tinfo.mrc_outfile, "\n");
  }
}

void dump_ipc_estimates(ThreadInfo &tinfo) {
  rewind(tinfo.ipc_outfile);

  // Smoothen before dumping:
  double prevValue = 0.0;
  for (int j = 0; j < tinfo.mrc_est_index + 1; j++) {
    for (int i = 1; i < tinfo.ipcCurveEstimates.n_rows; i++) {
      prevValue = tinfo.ipcCurveEstimates(i - 1, j);
      tinfo.ipcCurveEstimates(i, j) =
          std::max(prevValue, tinfo.ipcCurveEstimates(i, j));
    }
  }

  // dump to output file of online samples for this thread
  for (int i = 0; i < tinfo.ipcCurveEstimates.n_rows; i++) {
    for (int j = 0; j < tinfo.mrc_est_index + 1; j++) {
      fprintf(tinfo.ipc_outfile, "%f ", tinfo.ipcCurveEstimates(i, j));
    }
    fprintf(tinfo.ipc_outfile, "\n");
  }
}

void generate_profiling_plan(int cacheCapacity) {

    arma::vec plan;
    switch (cacheCapacity) {
    // Modify "plan" if you want DynaWay to sample cache sizes differently
    // Add cases if more/different cache capacities are passible
    // Example: with 12 ways, the plan is to allocate the target thread
    // 11->8->6->4->2->1 way(s). We interpolate data in between.
    case 16:
      plan = { 15, 15, 11, 7, 4, 2, 1 };
      break;
    case 15:
      plan = { 14, 14, 10, 7, 4, 2, 1 };
      break;
    case 14:
      plan = { 13, 13, 9, 6, 4, 2, 1 };
      break;
    case 13:
      plan = { 12, 12, 9, 6, 4, 2, 1 };
      break;
    case 12:
      plan = { 11, 11, 8, 6, 4, 2, 1 };
      break;
    case 11:
      plan = { 10, 10, 8, 6, 4, 2, 1 };
      break;
    case 10:
      plan = { 9, 9, 8, 6, 4, 2, 1 };
      break;
    case 9:
      plan = { 8, 8, 6, 4, 3, 2, 1 };
      break;
    case 8:
      plan = { 7, 7, 6, 4, 3, 2, 1 };
      break;
    case 7:
      plan = { 6, 6, 5, 4, 3, 2, 1 };
      break;
    case 6:
      plan = { 5, 5, 4, 3, 2, 1 };
      break;
    case 5:
      plan = { 4, 4, 3, 2, 1 };
      break;
    case 4:
      plan = { 3, 3, 2, 1 };
      break;
    case 3:
      plan = { 2, 2, 1 };
      break;
    default:
      LOG(ERROR) << "Invalid cache capacity passed to generate_profiling_plan().";
      std::exit(1);
    }

    num_ways_to_sample = (int)plan.size();
    LOG(INFO) << "[Partthyme] Num curve points to sample = " << num_ways_to_sample;

    // Initialize data structures that depend on number of ways to sample
    allAppsCacheAssignments = zeros<arma::cube>(2, state.cache_num_ways, num_ways_to_sample);
    for (auto it : tid_map) {
        ThreadInfo &tinfo = *(it.second);
        tinfo.xPoints = arma::linspace<arma::vec>(0, 0, num_ways_to_sample);
        tinfo.yPoints_ipc = arma::linspace<arma::vec>(0, 0, num_ways_to_sample);
        tinfo.yPoints_mpki = arma::linspace<arma::vec>(0, 0, num_ways_to_sample);
    }


    int row = -1;
    int idx = -1;
    arma::mat A(2, cacheCapacity);
    int sliceIdx = 0;

    for (int s = 0; s < num_ways_to_sample; s++) {
        // E.g.: if cache capacity = 6, plan = {5, 5, 4, 3, 2, 1};
        int p = plan[s];           // e.g. 5 (ways being profiled for target app)
        int r = cacheCapacity - p; // e.g. 1 (remaining ways for rest of apps)

        row = 0;
        idx = 0;
        for (int i = 0; i < p; i++) {
            A(row, idx) = 1;
            idx++;
        }
        for (int i = 0; i < r; i++) {
            A(row, idx) = 0;
            idx++;
        }
        //e.g. init: "1, 1, 1, 1, 1, 0"

        row = 1;
        idx = 0;
        for (int i = 0; i < p; i++) {
            A(row, idx) = 0;
            idx++;
        }
        for (int i = 0; i < r; i++) {
            A(row, idx) = 1;
            idx++;
        }
        //e.g. init: "1, 0, 0, 0, 0, 0"

        allAppsCacheAssignments.slice(sliceIdx) = A;
        sliceIdx++;
    }

    if (cacheCapacity == 12) {
        // workaround CAT bug with buckets 10,11 -- Broadwell 1540D processor specific bug
        A = { { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
              { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } }; // 0:11, 1:1
        allAppsCacheAssignments.slice(0) = A;
        allAppsCacheAssignments.slice(1) = A;
    }
}

// Convert integer list of cbm entries to a bitvector
uint32_t getCbm(const std::vector<int> &cbm_entries) {
  uint32_t cbm = 0;
  for (uint32_t c : cbm_entries) {
    cbm |= 1U << c;
  }
  return cbm;
}


void set_cacheways_to_cores(arma::mat C, int procIdxProfiled) {
    // E.g.  C = {  {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    //           {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1} }; // 0:1, 1:11
    std::string waysString;
    int cosID, numWaysBeingSampled, status;
    arma::mat cosMap = zeros<arma::mat>(C.n_rows, 2);

    // cosID = 1 has the sampled way string,
    // cosID = 2 should have the other way string with all remaining processes
    // sharing these ways ..
    CATController catCtrl(true);
    for (int c = 0; c < C.n_rows; c++) {
        cosID = c + 1;
        std::vector<int> cbm_entries;
        numWaysBeingSampled = 0;
        for (int j = 0; j < C.n_cols; j++) {
            if (C(c, j) > 0) {
                numWaysBeingSampled++;
                cbm_entries.emplace_back(j);
            }
        }
        catCtrl.setCbm(cosID, getCbm(cbm_entries));
        //if(enableLogging){ printf("[INFO] Changing cache alloc for cos %d to %s ways. Status= %d \n", cosID, waysString.c_str(), status); }

        //Indicate that this process is now sampling "x" number of cache ways
        //currentlySampling[cosID] = numWaysBeingSampled;
        cosMap(c, 0) = numWaysBeingSampled;
        cosMap(c, 1) = 1;
        if(enableLogging){
            LOG(DEBUG) << "[Partthyme] Indicating that COS " << cosID
            << " is now mapped to " << numWaysBeingSampled << " ways.";
        }
    }

    //Now map: (1) the profiled process (id: procIdxProfiled) to COS1,
    cosID = 1; //Assumption: profiled process will be mapped to COS1
    catCtrl.setCos(procIdxProfiled, cosID);
    if (enableLogging) {
        LOG(DEBUG) << "[Partthyme] Changing CORE " << procIdxProfiled
        << " map to COS " << cosID;
    }

    currentlySampling(procIdxProfiled, 0) =
        cosMap(cosID-1, 0); //numWaysBeingSampled;
    currentlySampling(procIdxProfiled, 1) = 1;

    // (2) everyone else to COS2 to share the remaining ways
    cosID = 2; //Assumption: rest of processes will be sharing cache ways in COS 2

    for (int procID = 0; procID < state.num_logical_cores; procID++) {
        if (procID == procIdxProfiled)
            continue;

        catCtrl.setCos(procID, cosID);
        if(enableLogging) {
            LOG(DEBUG) << "[Partthyme] Changing CORE " << procID
            << " map to COS " << cosID;
        }

        //Indicate that this process is now sampling "x" number of cache ways
        currentlySampling(procID, 0) = cosMap(cosID-1, 0); //numWaysBeingSampled;
        currentlySampling(procID, 1) = 1;
        if(enableLogging){
            LOG(DEBUG) << "[Partthyme] Indicating that pid " << procID
            << " is now sampling " << numWaysBeingSampled << " ways.";
        }
    }
}


std::vector<int> parse_cpuset(const cpu_set_t *cpuset) {
    std::vector<int> cores;
    int remaining = CPU_COUNT(cpuset);
    int cpu = 0;
    while (remaining) {
        if (CPU_ISSET(cpu, cpuset)) {
          --remaining;
          cores.push_back(cpu);
        }
        ++cpu;
    }

    return cores;
}

void print_core_assignments() {
    for (auto it : tid_map) {
        ThreadInfo &tinfo = *(it.second);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        if (sched_getaffinity(tinfo.tid, sizeof(cpu_set_t), &cpuset) == -1) {
            err(-1, "[DATAMIME-PROFILER] Thread %d sched_getaffinity() failed", tinfo.tidx);
        }

        std::vector<int> cores = parse_cpuset(&cpuset);

        LOG(INFO) << "[Partthyme] [Thread " << tinfo.tidx << " (tid "
        << tinfo.tid << ")] Core mapping: ";
        for (int c : cores) {
          uint64_t rmid = cmtCtrl.getRmid(c);
          LOG(INFO) << c << "(rmid " << rmid << ") | ";
          //printf("%d (rmid %lu) | ", c, rmid);
        }
    }

    fflush(stdout);
}

// ---------------------------------------------------------- //

uint64_t rdtsc(){
    unsigned int lo,hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

int tgkill(int tgid, int tid, int signal) {
    return syscall(SYS_tgkill, tgid, tid, signal);
}

// full of magic incantations learned from perf_examples/perf_util.c
void read_counters(ThreadInfo &tinfo, int fd) {
    updateMemTraffic(tinfo);
    updateCacheOccupancy(tinfo);

    auto group_it = tinfo.event_groups.find(fd);
    assert(group_it != tinfo.event_groups.end());

    auto* event_group = group_it->second;

    struct perf_event_header ehdr;
    auto* fds = event_group->fds;

    int id = perf_fd2event(fds, event_group->num_events, event_group->fd);
    if (id == -1) {
        LOG(ERROR) << "no event associated with fd = " << event_group->fd;
        std::exit(1);
    }

    int ret = perf_read_buffer(fds + id, &ehdr, sizeof(ehdr));
    if (ret) {
        LOG(ERROR) << "cannot read event header";
        std::exit(1);
    }

    if (ehdr.type != PERF_RECORD_SAMPLE) {
        LOG(WARNING) << "unexpected sample type = " << ehdr.type << ", skipping";
        return;
    }

    uint64_t size = ehdr.size - sizeof(ehdr);

    struct __attribute__ ((__packed__)) {
        uint64_t nanoseconds;
        uint32_t cpu;
        uint32_t reserved;
        uint64_t nr;
        uint64_t time_enabled;
        uint64_t time_running;
    } sample;

    ret = perf_read_buffer(fds, &sample, sizeof(sample));
    if (ret != 0) { warnx("could not read event info"); return; }
    assert(sample.nr == event_group->num_events);

    size -= sizeof(sample);

    uint64_t val;

    // using perf_event_mmap_page->time_mult and time_shift we can go from
    // sample.timestamp to rdtsc (lossily)
    struct perf_event_mmap_page* hdr = (struct perf_event_mmap_page*)fds[0].buf;

    __uint128_t x = sample.nanoseconds;
    x <<= hdr->time_shift;
    x /= hdr->time_mult;

    fprintf(event_group->outfile, "%d %d %d %ld %ld %ld %ld",
            event_group->fd,
            sample.cpu,
            event_group->tid,
            sample.nanoseconds,
            (uint64_t)x, //- a bit wrong but not super wrong
            sample.time_enabled,
            sample.time_running);

    // hrlee: I changed my mind. These are actually useful even in non-mrc est. mode.
    // Especially for mem. bw tracking.
    // But don't print out the names since it makes conversion to CSV difficult.
    fprintf(event_group->outfile, " %ld", tinfo.memTrafficTotal);
    fprintf(event_group->outfile, " %ld", tinfo.avgCacheOccupancy);

    if (tinfo.values.size() < tinfo.num_permanent_events + tinfo.num_clock_events)
        tinfo.values.resize(tinfo.num_permanent_events + tinfo.num_clock_events);

    for (int i = 0; i < sample.nr; i++) {
        ret = perf_read_buffer_64(fds, &val);
        if (ret != 0) { warnx("could not read %s", fds[i].name); return; }

        if (i < tinfo.num_permanent_events + tinfo.num_clock_events)
            tinfo.values[i] = val;

        fprintf(event_group->outfile, " %ld", val);
        size -= sizeof(val);
    }


    fprintf(event_group->outfile, "\n");
    //fflush(event_group->outfile); // Don't do this due to performance concerns

    if (size) {
        warnx("%zu bytes of leftover data", size);
        perf_skip_buffer(fds, size);
    }

    if (tinfo.phases_with_current_group >= PHASES_BETWEEN_SWITCHES
        && !args.mrc_est_mode) {
        tinfo.phases_with_current_group = 0;
        if (++group_it == tinfo.event_groups.end()) {
            group_it = tinfo.event_groups.begin();
        }
        int new_fd = group_it->first;

        int ret = ioctl(new_fd, PERF_EVENT_IOC_REFRESH, PHASES_BETWEEN_SWITCHES);
        if (ret == -1) {
            LOG(ERROR) << "cannot refresh";
            std::exit(1);
        }
    }

}

void sigthyme_handler(int n, siginfo_t* info, void* vsc) {
    int fd = info->si_fd;
    ThreadInfo &tinfo = *tid_map[fd_map[fd]];
    assert(!tinfo.is_dummy_thread);

    tinfo.phases++;
    tinfo.phases_with_current_group++;

    if (tinfo.phases % 100 == 0) {
        LOG(INFO) << "[DATAMIME-PROFILER] "
            << tinfo.tid
            << " completed "
            << tinfo.phases
            << " phases";
    }

    // Just keep updating relevant counters if MRC monitoring hasn't started yet.
    if (!monitorStartFlag && tinfo.phases > 1) {
        tinfo.lastCyclesCtr = tinfo.values[2];
        tinfo.lastInstrCtr = tinfo.values[1];
        tinfo.lastMemTrafficCtr = tinfo.memTrafficTotal;
    }

    if (tinfo.phases % mrc_invoke_monitor_len == 0 && args.mrc_est_mode) {
        tinfo.lastCyclesCtr = tinfo.values[2];
        tinfo.lastInstrCtr = tinfo.values[1];
        tinfo.lastMemTrafficCtr = tinfo.memTrafficTotal;
        if (tinfo.tidx == 0 && (tinfo.phases < args.num_phases)) { //Master
            if (enableLogging) {
                LOG(DEBUG) << "\n[DATAMIME-PROFILER] Master thread invokes beginning of profiling for "
                              "PROC " << thr_idx_profiled_global << ", PHASE " << tinfo.phases;
            }

            if (firstMRCInvocation) {
                mrc_invoke_monitor_len = mrc_profile_interval;
                firstMRCInvocation = false;
            }

            // Start dummy thread(s)
            LOG(INFO) << "[DATAMIME-PROFILER] Starting dummy thread to fill up shared ways\n";
            enable_array_scans_m.lock();
            enable_array_scans = true;
            enable_array_scans_m.unlock();

            monitorStartFlag = true;
            sampleSlicesIdx = 0;
            arma::mat C = allAppsCacheAssignments.slice(sampleSlicesIdx);
            //Slice has all cache assignments in a form of a matrix
            //Each row in the matrix corresponds to a given COS assignment

            set_cacheways_to_cores(C, thr_idx_profiled_global);
            sampleSlicesIdx++;
        }
    } else if (monitorStartFlag && (tinfo.phases % monitorLen == 0)) {
        tinfo.xPoints[(sampleSlicesIdx - 1)] = currentlySampling(tinfo.tidx, 0);

        //BUG: APM8 w/onlineProf: sometimes counters don't get updated even though
        //process moved to next phase!
        //Workaround: Enable hyperthreading and pin KPart to a thread not being used
        //by a process being profiled
        if ((double)(tinfo.values[1] - tinfo.lastInstrCtr) == 0) {
            LOG(ERROR) << " ### BUG ALERT WITH H/W COUNTERS ### "
                "tinfo.tidx = " << tinfo.tidx
                << ", tinfo.numPhases = " << tinfo.phases
                << ", tinfo.values[1] (instr) = " << (double)tinfo.values[1]
                << ", tinfo.values[2] (cycles) = " << (double)tinfo.values[2];
            currentlySampling(tinfo.tidx, 1) = 5; //Mark as incomplete with error ..
        } else { //Collect counters and mark as collected
            tinfo.yPoints_ipc[(sampleSlicesIdx - 1)] =
                (double)(tinfo.values[1] - tinfo.lastInstrCtr) /
                (double)(tinfo.values[2] - tinfo.lastCyclesCtr);
                double misses = (double)(tinfo.memTrafficTotal -
                                     tinfo.lastMemTrafficCtr) / state.cache_line_size;
            tinfo.yPoints_mpki[(sampleSlicesIdx - 1)] =
                misses * 1000 / (tinfo.values[1] - tinfo.lastInstrCtr);
            currentlySampling(tinfo.tidx, 1) = 0; //Collected, mark as completed!
        }

        tinfo.lastCyclesCtr = tinfo.values[2];
        tinfo.lastInstrCtr = tinfo.values[1];
        tinfo.lastMemTrafficCtr = tinfo.memTrafficTotal;

        if (enableLogging) {
            LOG(DEBUG) << "[DATAMIME-PROFILER] tinfo.tidx = " << tinfo.tidx
                << ", tinfo.phases = " << tinfo.phases
                << ", sampledWays = " << tinfo.xPoints[(sampleSlicesIdx - 1)]
                << ", sampledIPC = " << tinfo.yPoints_ipc[(sampleSlicesIdx - 1)]
                << ", sampledMPKI = " << tinfo.yPoints_mpki[(sampleSlicesIdx - 1)];
        }

        if (sampleSlicesIdx == num_ways_to_sample && currentlySampling(tinfo.tidx, 1) != 5) {
            // Use collected MRC samples to estimate MRC only for one profiled process
            if (tinfo.tidx == thr_idx_profiled_global) {
                if (loggingMRCFlags(tinfo.tidx, 0) < 1) { //If this proc hasn't logged yet, log MRC
                    if (enableLogging)
                        LOG(DEBUG) << "[In Thread " << tinfo.tidx << " - DONE SAMPLING]";

                    //Print xpoints and ypoints then interpolate to derive linear function
                    arma::vec xx = arma::linspace<vec>(1, state.cache_num_ways, state.cache_num_ways);
                    arma::vec yyMrc = tinfo.mrcEstimates.col(tinfo.mrc_est_index);
                    arma::vec yyIpc = tinfo.ipcCurveEstimates.col(tinfo.mrc_est_index);

                    // Need to ignore the first reading because it's only warmup period
                    // Consider the second reading only
                    tinfo.xPoints.at(0) = tinfo.xPoints.at(1);
                    tinfo.yPoints_mpki.at(0) = tinfo.yPoints_mpki.at(1);
                    tinfo.yPoints_ipc.at(0) = tinfo.yPoints_ipc.at(1);

                    // Interpolate to estimate the remaining points on the curves
                    interp1(tinfo.xPoints, tinfo.yPoints_mpki, xx, yyMrc, "linear");
                    interp1(tinfo.xPoints, tinfo.yPoints_ipc, xx, yyIpc, "linear");

                    tinfo.mrcEstimates.col(tinfo.mrc_est_index) = yyMrc;
                    tinfo.mrcEstimates.col(tinfo.mrc_est_index)[(state.cache_num_ways - 1)] =
                        tinfo.mrcEstimates.col(tinfo.mrc_est_index)[(state.cache_num_ways - 2)];

                    tinfo.ipcCurveEstimates.col(tinfo.mrc_est_index) = yyIpc;
                    tinfo.ipcCurveEstimates.col(tinfo.mrc_est_index)[(state.cache_num_ways - 1)] =
                        tinfo.ipcCurveEstimates.col(tinfo.mrc_est_index)[(state.cache_num_ways - 2)];

                    //Dump estimates to file to analyze later
                    dump_mrc_estimates(tinfo);
                    dump_ipc_estimates(tinfo);

                    loggingMRCFlags(tinfo.tidx, 0) = 1;

                    int startCol = std::max(0, (tinfo.mrc_est_index - state.HIST_WINDOW_LENGTH));
                    int endCol = tinfo.mrc_est_index;
                    double sum, count, avg;

                    //calc avg MRC curves
                    for (int w = 0; w < state.cache_num_ways; w++) {
                        sum = 0.0;
                        count = 0.0;
                        avg = 0.0;
                        for (int j = startCol; j <= endCol; j++) {
                            sum += tinfo.mrcEstimates(w, j);
                            count++;
                        }
                        avg = sum / count;
                        tinfo.mrcEstAvg[w] = avg;
                    }

                    //calc avg IPC curves
                    for (int w = 0; w < state.cache_num_ways; w++) {
                        sum = 0.0;
                        count = 0.0;
                        avg = 0.0;
                        for (int j = startCol; j <= endCol; j++) {
                            sum += tinfo.ipcCurveEstimates(w, j);
                            count++;
                        }
                        avg = sum / count;
                        tinfo.ipcCurveAvg[w] = avg;
                    }

                    // (FIXME) hrlee: Remove debug messages that I cannot easily
                    // integrate into easylogging++ for now. Must be added back
                    // in at later point.
                    //if (enableLogging) {
                    //    printf(" ---- tinfo.mrcEstimates() ---- \n");
                    //    tinfo.mrcEstimates
                    //        .cols(std::max(0, (tinfo.mrc_est_index - state.HIST_WINDOW_LENGTH)),
                    //              tinfo.mrc_est_index).print();
                    //    printf(" ---- tinfo.ipcCurveEstimates() ---- \n");
                    //    tinfo.ipcCurveEstimates
                    //        .cols(std::max(0, (tinfo.mrc_est_index - state.HIST_WINDOW_LENGTH)),
                    //              tinfo.mrc_est_index).print();
                    //} else {
                        tinfo.mrcEstimates
                            .cols(std::max(0, (tinfo.mrc_est_index - state.HIST_WINDOW_LENGTH)),
                                  tinfo.mrc_est_index);
                        tinfo.ipcCurveEstimates
                            .cols(std::max(0, (tinfo.mrc_est_index - state.HIST_WINDOW_LENGTH)),
                                  tinfo.mrc_est_index);
                    //}

                    tinfo.mrc_est_index++;

                    //Store globally
                    sampledMRCs.col(tinfo.tidx) = tinfo.mrcEstAvg;
                    sampledIPCs.col(tinfo.tidx) = tinfo.ipcCurveAvg;

                    /*
                    if (enableLogging) {
                        printf("\n -- sampledMRCs -- \n");
                        sampledMRCs.print();

                        printf("\n -- sampledIPCs -- \n");
                        sampledIPCs.print();
                    }
                    */

                } //end if(loggingMRCFlags(tinfo.tidx,0) < 1){  //If this proc hasn't
                //logged yet, log MRC

            } //end if( tinfo.tidx == thr_idx_profiled_global )

            if (loggingMRCFlags(thr_idx_profiled_global, 0) == 1) {
                if (enableLogging)
                    LOG(DEBUG) << "[INFO] Profiling done for PROC " << thr_idx_profiled_global;

                loggingMRCFlags.zeros(); //= zeros<arma::vec>(state.num_logical_cores);
                sampleSlicesIdx = 0;     //Start over
                thr_idx_profiled_global++;

                if (thr_idx_profiled_global >= num_profiled_threads) {
                    thr_idx_profiled_global = 0;
                    monitorStartFlag = false;
                    LOG(INFO) << "[DATAMIME-PROFILER] Halting dummy thread to fill up shared ways";
                    enable_array_scans_m.lock();
                    enable_array_scans = false;
                    enable_array_scans_m.unlock();
                    cache_utils::share_all_cache_ways(state.num_logical_cores, state.cache_num_ways);
                }

            }
        }          //end if( sampleSlicesIdx == numWaysToSample )
        else { //get new allocated cache ways to sample
            if (tinfo.tidx == thr_idx_profiled_global) {
                //if (enableLogging) {
                //  currentlySampling.print();
                //}
                if (currentlySampling(thr_idx_profiled_global, 1) == 0) { //profiled process done?
                    if (enableLogging) {
                        LOG(DEBUG) << "[DATAMIME-PROFILER] Master thread invokes NEXT profiling plan .";
                    }
                    arma::mat C = allAppsCacheAssignments.slice(sampleSlicesIdx);
                    set_cacheways_to_cores(C, thr_idx_profiled_global);
                    sampleSlicesIdx++;
                } else {
                    //Still some processes didn't collect MRC/IPC...
                    LOG(DEBUG) << "[DATAMIME-PROFILER] Wait as some threads didn't collect their mrc/ipc samples";
                }
            }
        }

    }

    read_counters(tinfo, fd);

    // Only detect termination condition for the leading thread (tidx == 0)
    // so that we don't have to deal with detaching one thread while another
    // thread reaches the termination condition concurrently.
    if (tinfo.tidx == 0 && tinfo.phases >= args.num_phases) {
        LOG(DEBUG) << "[DATAMIME-PROFILER] Thread " << tinfo.tid << " in Thread Group "
        << tinfo.tgid << " hit " << tinfo.phases << " phases. Exiting.";
        state.done = true;
        if (state.first_finished_thread == -1) {
            state.first_finished_thread = tinfo.tid;
            auto ret = tgkill(tinfo.tgid, tinfo.tid, SIGSTOP);
            if (ret != 0)
                LOG(ERROR) << "[DATAMIME-PROFILER] tgkill failed: " << strerror(errno);
        }

        return;
    }

}

std::string ThreadInfo::filter_events(const char* events) {
    std::stringstream events_input(events);
    std::stringstream filtered_events;
    std::string sep= "";

    // go through all requested events
    while (events_input.good()) {
        std::string event;
        getline(events_input, event, ',');

        // check if the event is available on this machine
        pfm_perf_encode_arg_t arg;
        perf_event_desc_t fd;
        memset(&arg, 0, sizeof(arg));
        arg.attr = &fd.hw;
        arg.fstr = &fd.fstr;
        int ret = pfm_get_os_event_encoding(event.c_str(), PFM_PLM0|PFM_PLM3,
                                            PFM_OS_PERF_EVENT_EXT, &arg);

        if (ret != PFM_SUCCESS) {
            LOG(WARNING) << "[DATAMIME-PROFILER] event " << event.c_str()
                << " is not supported on this machine.";
            continue;
        }

        filtered_events << sep << event;
        sep = ",";
    }

    return filtered_events.str();
}


void initialize_event(perf_event_desc_t* event, bool is_leader, uint64_t phase_len) {
    event->hw.disabled = (is_leader) ? 1 : 0;
    event->hw.wakeup_events = 1;
    event->hw.pinned = (is_leader) ? 1 : 0;
    event->hw.sample_type = PERF_SAMPLE_READ | PERF_SAMPLE_TIME | PERF_SAMPLE_CPU;
    event->hw.sample_period = (is_leader) ? phase_len : (1L << 62);
    if (is_leader) {
        event->hw.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_SCALE;
    }
}

EventGroup::EventGroup(perf_event_desc_t* clock_event_fd,
                       perf_event_desc_t* permanent_event_fds,
                       int profile_tid, FILE *profile_outfile,
                       int num_permanent_events) {
    tid = profile_tid;
    outfile = profile_outfile;

    memcpy(&fds[0], clock_event_fd, sizeof(perf_event_desc_t));
    memcpy(&fds[1], permanent_event_fds, sizeof(perf_event_desc_t) * num_permanent_events);

    num_events = 1 + num_permanent_events;

    fds[0].fd = -1; // event group leader
    for (int i = 0; i < num_events; i++) {
        fds[i].fd = perf_event_open(&fds[i].hw, tid, -1, fds[0].fd, 0);
        if (fds[i].fd < 0) {
            printf("[DATAMIME-PROFILER] pfm_event_open returned invalid fd: %s\n", pfm_strerror(fds[i].fd));
            LOG(ERROR) << "[DATAMIME-PROFILER] pfm_event_open returned invalid fd: " << pfm_strerror(fds[i].fd);
            LOG(ERROR) << "Could not open required event: " << fds[i].name;
            std::exit(1);
        }
    }
    fd = fds[0].fd; // [hrlee] fd is that of group leader!
    // Add fd -> tid mapping
    // [hrlee] Ideally I would also like to keep a fd->ThreadInfo mapping,
    // but since the EventGroup object is a member of ThreadInfo, I didn't
    // like that such a self-referential mapping existed.
    LOG(INFO) << "Adding fd = " << fd << " to tid = " << tid << " mapping";
    fd_map.emplace(fd, tid);

    fds[0].buf = mmap(NULL, (1 + BUFFER_PAGES) * PAGE_SIZE,
                             PROT_READ | PROT_WRITE, MAP_SHARED,
                             fds[0].fd, 0);
    if (fds[0].buf == MAP_FAILED) {
        LOG(ERROR) << "cannot mmap buffer";
        std::exit(1);
    }

    fds[0].pgmsk = (BUFFER_PAGES * PAGE_SIZE) - 1;

    int ret = fcntl(fds[0].fd, F_SETFL, fcntl(fds[0].fd, F_GETFL, 0) | O_ASYNC);
    if (ret == -1) {
        LOG(ERROR) << "cannot set ASYNC";
        std::exit(1);
    }

    ret = fcntl(fds[0].fd, F_SETSIG, SIGTHYME);
    if (ret == -1) {
        LOG(ERROR) << "cannot setsig";
        std::exit(1);
    }

    ret = fcntl(fds[0].fd, F_SETOWN, getpid());
    if (ret == -1) {
    LOG(ERROR) << "cannot setown";
    std::exit(1);
}

}

void EventGroup::finalize_events() {
if (enableLogging) {
    LOG(DEBUG) << "[DATAMIME-PROFILER] Inside finalize_event for thread "
            << tid << ", group " << fd;
    }
    fprintf(outfile, "group %d", fd);
    for (int i = 0; i < num_events; i++) {
        fprintf(outfile, " %s", fds[i].name);
    }
    // Don't print out the names of RDT counters since they are not grouped events
    //fprintf(outfile, " %s", lmbName.c_str());
    //fprintf(outfile, " %s", l3OccupName.c_str());
    fprintf(outfile, "\n");
}

bool EventGroup::add_event(perf_event_desc_t* event_fd) {
    if (num_events >= MAX_GROUP_EVENTS) return false;

    memcpy(&fds[num_events], event_fd, sizeof(perf_event_desc_t));
    fds[num_events].fd = perf_event_open(&fds[num_events].hw, tid, -1, fds[0].fd, 0);
    if (fds[num_events].fd < 0) {
        LOG(WARNING) << "Cannot perf_event_open()";
        return false;
    }

    LOG(INFO) << "[DATAMIME-PROFILER] successfully added event: "
        << fds[num_events].name << "to group " << fd;

    num_events++;
    return true;
}

void attach() {
    for (auto it : tid_map) {
        ThreadInfo &tinfo = *(it.second);
        if (!tinfo.is_dummy_thread) {
            if (ptrace(PTRACE_SEIZE, tinfo.tid, 0, 0) != 0) {
                LOG(ERROR) << "could not PTRACE_SEIZE Thread "
                    << tinfo.tidx << "(tid " << tinfo.tid << ")\n";
                std::exit(1);
            }
            LOG(INFO) << "[DATAMIME-PROFILER] Successfully attached to Thread "
                << tinfo.tidx << " (tid " << tinfo.tid << ")";
        }
    }
}

void ThreadInfo::create_event_groups() {
    std::string rotating_events;
    //rotating_events = filter_events(args.events);
    if (!args.mrc_est_mode)
        rotating_events = filter_events(args.events);
    else
        rotating_events = "";

    // create all event fds
    int ret = perf_setup_list_events(CLOCK_EVENT, &clock_event_fd, &num_clock_events);
    assert(ret == 0 && num_clock_events == 1);

    ret = perf_setup_list_events(PERMANENT_EVENTS, &permanent_event_fds, &num_permanent_events);
    assert(ret == 0);

    if (rotating_events.size() == 0) {
        num_rotating_events = 0;
    } else {

        ret = perf_setup_list_events(rotating_events.c_str(), &rotating_event_fds, &num_rotating_events);
        if (ret != 0) {
            LOG(ERROR) << "Could not initialize rotating events: " << pfm_strerror(ret);
            std::exit(1);
        }
    }

    if (enableLogging)
        LOG(DEBUG) << "[DATAMIME-PROFILER] Initializing template events for thread " << tid;

    // initialize template events
    initialize_event(&clock_event_fd[0], true, args.phase_len);
    for (int i = 0; i < num_permanent_events; i++) {
        initialize_event(&permanent_event_fds[i], false, args.phase_len);
    }
    for (int i = 0; i < num_rotating_events; i++) {
        initialize_event(&rotating_event_fds[i], false, args.phase_len);
    }

    if (enableLogging)
        LOG(DEBUG) << "[DATAMIME-PROFILER] Creating event groups for thread " <<  tid;
    // create event groups
    auto event_group = new EventGroup(clock_event_fd,
                                      permanent_event_fds,
                                      tid, outfile,
                                      num_permanent_events);
    auto pair = event_groups.emplace(event_group->fd, event_group);
    assert(pair.second);

    for (int i = 0; i < num_rotating_events; i++) {
        if (event_group->add_event(&rotating_event_fds[i])) {
            continue;
        }
        event_group = new EventGroup(clock_event_fd,
                                     permanent_event_fds,
                                     tid, outfile,
                                     num_permanent_events);
        auto pair = event_groups.emplace(event_group->fd, event_group);
        assert(pair.second);
        auto ret = event_group->add_event(&rotating_event_fds[i]);
        assert(ret);
    }

    if (enableLogging)
        LOG(DEBUG) << "[DATAMIME-PROFILER] Finalizing groups for thread " << tid;
    for (auto eg: event_groups) {
        eg.second->finalize_events();
    }

    // activate the first event group
    auto it = event_groups.begin();
    assert(it != event_groups.end());
    auto first_fd = it->first;

    if (enableLogging)
        LOG(DEBUG) << "[DATAMIME-PROFILER] Activating first event group "
            << first_fd << " for thread " << tid;

    if (args.mrc_est_mode)
        ret = ioctl(first_fd, PERF_EVENT_IOC_REFRESH, 1L << 62);
    else
        ret = ioctl(first_fd, PERF_EVENT_IOC_REFRESH, PHASES_BETWEEN_SWITCHES);
    if (ret == -1) {
        LOG(ERROR) << "cannot refresh";
        std::exit(1);
    }
}

void usage(char* argv[]) {
    std::cout << "USAGE:" << std::endl;
    std::cout << argv[0] << " -e <comma-sep-events> -l <phase_len> "
        "-n <num_phases> -f <outfile_prefix> -g <thread_group_id> "
        "[-m] [-h] -t <comma-sep-tids> ..." \
        << std::endl;
    std::cout << "\t-e <comma-sep-events> : list of events to profile" \
        << std::endl;
    std::cout << "\t-l <phase_len> : profile phase length in cycles" \
        << std::endl;
    std::cout << "\t-n <num_phases> : number of phases" <<std::endl;
    std::cout << "\t-w <mrc_warmup_period> : MRC warmup period (in Mcycles)" <<std::endl;
    std::cout << "\t-p <mrc_profile_period> : MRC profile period (in Mcycles)" <<std::endl;
    std::cout << "\t-f <outfile_prefix> : all generated output files will begin"
        " with this prefix" \
        << std::endl;
    std::cout << "\t-g <thread_group_id> : currently only supports profiling"
        " threads from a single thread group" \
        << std::endl;
    std::cout << "\t-t <comma-sep-tids> : must belong to same thread group" <<std::endl;
    std::cout << "\t-r <results_dir> : absolute path to results directory" \
        << std::endl;
    std::cout << "\t-d : Enable debug output to datamime-profiler.log" <<std::endl;
    std::cout << "\t-m : enable MRC estimation mode. In this mode, user-given"
        " events will not be tracked." \
        << std::endl;
    std::cout << "\t-h : Print this help message" << std::endl;
}


ThymeArgs parse_args(int argc, char* argv[]) {
    ThymeArgs args;
    int c;
    char *tids;
    while ((c = getopt(argc, argv, "e:l:n:w:p:f:g:t:r:dmh")) != -1) {
        switch(c) {
            case 'e':
                args.events = optarg;
                break;
            case 'l':
                args.phase_len = std::stoul(optarg);
                break;
            case 'n':
                args.num_phases = std::stoul(optarg);
                break;
            case 'w':
                args.mrc_warmup_period = std::stoul(optarg);
                break;
            case 'p':
                args.mrc_profile_period = std::stoul(optarg);
                break;
            case 'f':
                args.glob_outfile_name = optarg;
                break;
            case 'g':
                args.tgid = std::stoi(optarg);
                break;
            case 't':
                tids = optarg;
                break;
            case 'r':
                args.results_dir = optarg;
                break;
            case 'd':
                args.debug = true;
                break;
            case 'm':
                args.mrc_est_mode = true;
                break;
            case 'h':
                usage(argv);
                exit(0);
                break;
            case '?':
                usage(argv);
                exit(1);
                break;
        }
    }
    std::stringstream tids_input(tids);
    while (tids_input.good()) {
        std::string tid_s;
        std::getline(tids_input, tid_s, ',');
        args.profiled_tids.emplace_back(std::stoi(tid_s.c_str()));
    }

    num_profiled_threads = (int)(args.profiled_tids.size());
    mrc_warmup_interval = (args.mrc_warmup_period * 1e6) / args.phase_len;
    mrc_invoke_monitor_len = mrc_warmup_interval;
    mrc_profile_interval = (args.mrc_profile_period * 1e6) / args.phase_len;
    return args;
}

void fini_handler(int sig) {
    LOG(INFO) << "[DATAMIME-PROFILER] received signal " << sig << ", terminating...";
    cache_utils::share_all_cache_ways(state.num_logical_cores, state.cache_num_ways);
    tid_map.clear();
    exit(2);
}

void ThreadInfo::flush() {
    fflush(outfile);
    fflush(mrc_outfile);
    fflush(ipc_outfile);
    fclose(outfile);
    fclose(mrc_outfile);
    fclose(ipc_outfile);
}

void profile() {
    while (tid_map.size() > num_scan_threads) {
        if (enableLogging)
            LOG(DEBUG) << "[DATAMIME-PROFILER] currently profiling " << (int)(tid_map.size() - 1)
                << " threads";
        int status = 0;
        int pid = waitpid(-1, &status, __WALL | WSTOPPED);

        if (pid == -1) {
            LOG(ERROR) << "waitpid failed: returned " << pid;
            std::exit(1);
        }

        // handle normal application exits
        if (WIFEXITED(status)) {
            LOG(INFO) << "[DATAMIME-PROFILER] Traced thread " << pid << " terminated normally\n";
            // FIXME: call destructor after removing from here, and remove from
            // the fd->ThreadInfo mapping as well.
            tid_map[pid]->flush(); // Flush all outputs (Why does the destructor not do this for me?)
            tid_map.erase(pid);
            continue;
        }
        assert(WIFSTOPPED(status) || WIFSIGNALED(status));

        // handle stop signals sent to the traced process correctly
        if (WIFSTOPPED(status) && WSTOPSIG(status) != SIGSTOP) {
            int group_stop = (status >> 16 == PTRACE_EVENT_STOP);

            if (group_stop) {
                if (WSTOPSIG(status) == SIGTRAP) {
                    if (ptrace(PTRACE_CONT, pid, 0, 0) < 0) {
                        LOG(ERROR) << "ptrace(PTRACE_CONT)";
                        std::exit(1);
                    }
                } else {
                    if (ptrace(PTRACE_LISTEN, pid, 0, 0) < 0) {
                        LOG(ERROR) << "ptrace(PTRACE_LISTEN)";
                        std::exit(1);
                    }
                }
            } else {
                if (ptrace(PTRACE_CONT, pid, 0, WSTOPSIG(status)) < 0) {
                    LOG(ERROR) << "ptrace(PTRACE_CONT)";
                    std::exit(1);
                }
            }
            continue;
        }

        // handle reaching the desired number of phases by detaching all
        // profiled threads
        if (state.done) {
            LOG(INFO) << "[DATAMIME-PROFILER] Start winding down...";
            LOG(INFO) << "[DATAMIME-PROFILER] " << (int)(tid_map.size() - 1)
                << " threads left.";

            // Detach from thread when we get a SIGSTOP
            if (ptrace(PTRACE_DETACH, pid, 0, 0) < 0) {
                LOG(ERROR) << "ptrace(PTRACE_DETACH";
                std::exit(1);
            }

            tid_map[pid]->flush(); // Flush all outputs (Why does the destructor not do this for me?)
            tid_map.erase(pid);
            LOG(INFO) << "[DATAMIME-PROFILER] detached from thread " << pid
                << ". " << (int)(tid_map.size() - 1) << " threads left.";

            if (pid == state.first_finished_thread) {
                // if we're the first one done, we need to detach others
                for (auto it : tid_map) {
                    ThreadInfo &tinfo = *(it.second);
                    if (!tinfo.is_dummy_thread) {
                        LOG(DEBUG) << "[DATAMIME-PROFILER] sending SIGSTOP to thread "
                        << tinfo.tid;
                        tgkill(tinfo.tgid, tinfo.tid, SIGSTOP);
                    }
                }
            }

            continue;
        }

        if (WIFSIGNALED(status)) {
            ptrace(PTRACE_CONT, pid, 0, WTERMSIG(status));
            tid_map[pid]->flush(); // Flush all outputs (Why does the destructor not do this for me?)
            tid_map.erase(pid);
            continue;
        }

        LOG(ERROR) << "unexpected state: pid = "
            << pid
            << "status = "
            << status;
        std::exit(1);
    }
}

int create_scan_threads(int cur_tidx) {
    enable_array_scans = false;
    num_scan_threads = 0;

    pthread_t scan_thread;
    int *arg = (int*)(malloc(sizeof(*arg)));
    *arg = cur_tidx;

    int tret = pthread_create(&scan_thread, NULL, scan_array, arg);
    if (tret != 0) {
        LOG(ERROR) << "Could not create dummy thread "
            << cur_tidx
            << ": return code from pthread_create()="
            << tret;
        std::exit(1);
    }

    // Wait until all array-scanning threads are active to ensure that their
    // tids are available, since there isn't a way for us to
    // get the tid of another thread.
    while (num_scan_threads < 1);
    assert(scan_thread_tid != -1);

    tid_map.emplace(scan_thread_tid, new ThreadInfo());
    // FIXME: Remove this mess with actual proper constructor...
    ThreadInfo &tinfo = *(tid_map[scan_thread_tid]);
    tinfo.tidx = cur_tidx;
    tinfo.tid = scan_thread_tid;
    tinfo.tgid = gettid();
    std::vector<int> cores;
    cores.push_back(state.assignable_cores.front());
    state.assignable_cores.erase(state.assignable_cores.begin());
    tinfo.cores = cores;
    tinfo.rmid = cur_tidx + 1;
    tinfo.is_dummy_thread = true;

    tinfo.lastInstrCtr = 0;
    tinfo.lastCyclesCtr = 0;
    tinfo.lastMemTrafficCtr = 0;
    tinfo.memTrafficLast = 0;
    tinfo.memTrafficTotal = 0;
    tinfo.avgCacheOccupancy = 0;

    return ++cur_tidx;
}

INITIALIZE_EASYLOGGINGPP

int main(int argc, char* argv[]) {
    state.cache_line_size = (int)sysconf(_SC_LEVEL3_CACHE_LINESIZE);
    state.num_logical_cores = (int)sysconf(_SC_NPROCESSORS_CONF);
    state.cache_num_ways = (int)sysconf(_SC_LEVEL3_CACHE_ASSOC);

    if (numa_available() < 0)
        errx(1, "Your system must support libnuma\n");

    // Assign threads to cores at NUMA node zero for simplicity of managing
    // cache allocation and memory bandwidth monitoring.
    // Assumption: 1-to-1 mapping between sockets and numa nodes, which is
    // generally true for Xeon processors...
    printf("Assignable cores (i.e., those in NUMA node 0): ");
    for (int c = 0; c < state.num_logical_cores; c++) {
        if (numa_node_of_cpu(c) == 0) {
            state.assignable_cores.emplace_back(c);
            printf("%d ", c);
        }
    }
    printf("\n");

    if (argc > 7 + (int)state.assignable_cores.size() - 1) {
        usage(argv);
        errx(1, "Cannot support profiling more than %d simultaneous threads\n",
             state.num_logical_cores - 1);
    }

    args = parse_args(argc, argv);

    // Set up logging configuration
    // Could use a configuration file, but it is unlikely that we will change
    // the logging configuration often.
    std::string logfilename(args.results_dir);
    char& back = logfilename.back();
    if (back != '/')
        logfilename.append("/");
    logfilename.append(args.glob_outfile_name);
    logfilename.append("_datamime-profiler.log");
    el::Configurations logConf;
    logConf.setToDefault();

    // Info level
    logConf.set(el::Level::Info,
            el::ConfigurationType::Format, "%datetime -- %level -- %msg");
    logConf.set(el::Level::Info, el::ConfigurationType::ToFile, "true");
    logConf.set(el::Level::Info, el::ConfigurationType::Filename, logfilename.c_str());

    // Warning level
    logConf.set(el::Level::Warning,
            el::ConfigurationType::Format, "%datetime -- %level -- %msg");
    logConf.set(el::Level::Warning, el::ConfigurationType::ToFile, "true");
    logConf.set(el::Level::Warning, el::ConfigurationType::Filename, logfilename.c_str());

    // Error level
    logConf.set(el::Level::Error,
            el::ConfigurationType::Format, "%datetime -- %level -- %msg");
    logConf.set(el::Level::Error, el::ConfigurationType::ToFile, "true");
    logConf.set(el::Level::Error, el::ConfigurationType::Filename, logfilename.c_str());

    // Debug level
    logConf.set(el::Level::Debug,
            el::ConfigurationType::Format, "%datetime -- %level -- %msg");
    logConf.set(el::Level::Debug, el::ConfigurationType::ToFile, args.debug ? "true" : "false");
    logConf.set(el::Level::Debug, el::ConfigurationType::Filename, logfilename.c_str());
    logConf.set(el::Level::Debug, el::ConfigurationType::ToStandardOutput, "false");

    el::Loggers::reconfigureLogger("default", logConf);
    el::Logger* logger = el::Loggers::getLogger("default");

    // Print out logs that were deferred until log configs were set up
    if (args.mrc_est_mode)
        logger->info("[DATAMIME-PROFILER] MRC estimation mode enabled. Ignoring user-specified events list");

    logger->info("[DATAMIME-PROFILER] MRC estimation warmup period: %v M cycles", args.mrc_warmup_period);
    logger->info("[DATAMIME-PROFILER] MRC estimation profile period: %v M cycles", args.mrc_profile_period);

    // Set memory bind to node 0
    auto mask = numa_parse_nodestring("0");
    numa_bind(mask);

    // Check CMT support
    if (CMTController::cmtSupported())
        logger->info("[DATAMIME-PROFILER] CMT supported");
    else
        logger->info("[DATAMIME-PROFILER] CMT not supported");

    // Initialize global structures based on cache and core configuration
    sampledMRCs = zeros<arma::mat>(state.cache_num_ways, state.num_logical_cores);
    sampledIPCs = zeros<arma::mat>(state.cache_num_ways, state.num_logical_cores);
    loggingMRCFlags = zeros<arma::vec>(state.num_logical_cores);

    // (row,col) = How many ways we are sampling <row> CORE in <col> COS.
    // First column is how many ways each core is allocated
    // Second column is whether sampling has finished for the given CORE
    // 0 == finished, 1 == still sampling
    currentlySampling = zeros<arma::mat>(state.num_logical_cores, 2);

    // Printing out args
    logger->info("Events: %v", args.events);
    logger->info("glob_outfile_header: %v", args.glob_outfile_name);
    logger->info("phase_len: %v", args.phase_len);
    logger->info("num_phases: %v", args.num_phases);
    logger->info("tgid: %v", args.tgid);

    if (args.mrc_est_mode)
        cache_utils::share_all_cache_ways(state.num_logical_cores, state.cache_num_ways);

    int ret = pfm_initialize();
    if (ret != PFM_SUCCESS) {
        logger->fatal("Could not initialize library: %s", pfm_strerror(ret));
        std::exit(1);
    }

    // Track profiled thread information
    int tidx = 0; // assigned as threads are created (0, 1, 2, ...)
    for (int tid : args.profiled_tids) {
        tid_map.emplace(tid, new ThreadInfo());
        ThreadInfo &tinfo = *(tid_map[tid]);
        // Basic thread information
        tinfo.tidx = tidx;
        tinfo.tid = tid;
        tinfo.tgid = args.tgid;
        std::vector<int> cores;

        // MRC and IPC estimation related data structures
        tinfo.mrc_est_index = 0;

        tinfo.mrcEstAvg = zeros<arma::vec>(state.cache_num_ways);
        tinfo.mrcEstimates = zeros<arma::mat>(state.cache_num_ways, 1000);

        tinfo.ipcCurveAvg = zeros<arma::vec>(state.cache_num_ways);
        tinfo.ipcCurveEstimates = zeros<arma::mat>(state.cache_num_ways, 1000);

        // FIXME: Remove this mess with actual proper constructor...
        tinfo.lastInstrCtr = 0;
        tinfo.lastCyclesCtr = 0;
        tinfo.lastMemTrafficCtr = 0;
        tinfo.memTrafficLast = 0;
        tinfo.memTrafficTotal = 0;
        tinfo.avgCacheOccupancy = 0;

        // Assign each thread to its own logical core, since this makes
        // tracking local memory bandwidth and llc occupancy for each
        // thread easier.
        // TODO: Assign to all other cores (1 through num_logical_cores-1),
        // and find a way to manage rmid-to-core mapping more intelligently...
        cores.push_back(state.assignable_cores.front());
        state.assignable_cores.erase(state.assignable_cores.begin());
        tinfo.cores = cores;

        tinfo.rmid = tidx + 1;
        tidx++;

        // Open associated output files
        std::string results_dir_str(args.results_dir);
        char& back = results_dir_str.back();
        if (back != '/')
            results_dir_str.append("/");

        std::stringstream ss;
        ss << results_dir_str;
        if (args.mrc_est_mode)
            ss << args.glob_outfile_name << "_counters_" << tinfo.tid;
        else
            ss << args.glob_outfile_name << "_grouped_counters_" << tinfo.tid;

        logger->info("Open file: %v", ss.str().c_str());
        FILE *fd = fopen(ss.str().c_str(), "w");
        if (fd == nullptr) {
            logger->fatal("could not open output file for thread %d", tinfo.tid);
            std::cout << std::strerror(errno) << '\n';
            std::exit(1);
        }
        tinfo.outfile = fd;

        if (args.mrc_est_mode) {
            std::stringstream mrcss;
            mrcss << results_dir_str;
            mrcss << args.glob_outfile_name << "_mrc_" << tinfo.tid;
            logger->info("Open file: %v", mrcss.str().c_str());
            FILE *mrcfd = fopen(mrcss.str().c_str(), "w");
            if (mrcfd == nullptr) {
                std::cout << std::strerror(errno) << '\n';
                logger->fatal("could not open mrc file for thread %d", tinfo.tid);
                std::exit(1);
            }
            tinfo.mrc_outfile = mrcfd;

            std::stringstream ipcss;
            ipcss << results_dir_str;
            ipcss << args.glob_outfile_name << "_ipc_" << tinfo.tid;
            logger->info("Open file: %v", ipcss.str().c_str());
            FILE *ipcfd = fopen(ipcss.str().c_str(), "w");
            if (ipcfd == nullptr) {
                std::cout << std::strerror(errno) << '\n';
                logger->fatal("could not open ipc file for thread %d", tinfo.tid);
                std::exit(1);
            }
            tinfo.ipc_outfile = ipcfd;
        } else {
            tinfo.mrc_outfile = NULL;
            tinfo.ipc_outfile = NULL;
        }
    }

    generate_profiling_plan(state.cache_num_ways);

    create_scan_threads(tidx);

    // Set thread ids and CPU affinity
    for (auto it : tid_map) {
        ThreadInfo &tinfo = *(it.second);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int c: tinfo.cores)
            CPU_SET(c, &cpuset);
        logger->info("[DATAMIME-PROFILER] Setting Thread affinity for Thread %v (tid = %v)", tinfo.tidx, tinfo.tid);

        if (sched_setaffinity(tinfo.tid, sizeof(cpuset), &cpuset) == -1)
            err(-1, "[Thread %d] pthread_setaffinity_np() failed\n", tinfo.tidx);

        initCmt(tinfo);
    }

    // Pin the profiling thread to next available core
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    int mainthr_core = state.assignable_cores.front();
    state.assignable_cores.erase(state.assignable_cores.begin());
    logger->info("[DATAMIME-PROFILER] Pinning main thread to core %v", mainthr_core);
    CPU_SET(mainthr_core, &cpuset);
    if (sched_setaffinity(gettid(), sizeof(cpuset), &cpuset) == -1)
        err(-1, "[Thread %ld] pthread_setaffinity_np() failed\n", gettid());

    print_core_assignments();

    // install the sigthyme_handler
    struct sigaction act;
    memset(&act, '\0', sizeof(act));
    act.sa_sigaction = sigthyme_handler;
    act.sa_flags = SA_SIGINFO | SA_RESTART;
    ret = sigaction(SIGTHYME, &act, 0);
    if (ret != 0) {
        logger->fatal("could not install sigthyme_handler");
        std::exit(1);
    }

    auto old_handler = signal(SIGINT, fini_handler);
    if (old_handler == SIG_ERR) {
        logger->fatal("could not install fini_handler\n");
        std::exit(1);
    }

    attach();

    for (int tid : args.profiled_tids) {
        ThreadInfo &tinfo = *(tid_map[tid]);
        tinfo.create_event_groups();
    }

    for (int tid : args.profiled_tids) {
        ThreadInfo &tinfo = *(tid_map[tid]);
        logger->info("Thread %v number of event groups = %v", tid, tinfo.event_groups.size());
    }

    profile();

    pfm_terminate();

    logger->info("[DATAMIME-PROFILER] done!");
}
