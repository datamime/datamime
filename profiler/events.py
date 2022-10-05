# GNU GPL License
#
# Copyright (c) 2022 by Massachusetts Institute of Technology
#
# This file is part of Datamime.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# If you use this software in your research, we request that you reference
# the Datamime paper ("Datamime: Generating Representative Benchmarks by
# Automatically Synthesizing Datasets", Lee and Sanchez, MICRO-55, October 2022)
# as the source in any publications that use this software, and that you send
# us a citation of your work.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# hrlee: Choose a set of events that capture microarchitectural characteristics
# we are interested in.
# Note that Intel provides 3 fixed-function counters (UNHALTED_REFERENCE_CYCLES,
# INST_RETIRED.ANY, CPU_CLK_UNHALTED.THREAD) so these do not need to be explicitly
# captured.
# I've also differentiated skylake and broadwell events since they use different
# counters. (OLD_EVENTS are events used for very old runs of Datamime kept for
# backward compatibility when analyzing those results)

OLD_EVENTS = ",".join([
        "INST_RETIRED.k",
        "BR_INST_RETIRED",
        "BR_MISP_RETIRED",

        "UOPS_RETIRED.STALL_CYCLES",
        "UOPS_RETIRED.TOTAL_CYCLES",
        "RESOURCE_STALLS.ROB",

        "UOPS_EXECUTED.THREAD_CYCLES_GE_1",
        "UOPS_EXECUTED.THREAD_CYCLES_GE_2",
        "UOPS_EXECUTED.THREAD_CYCLES_GE_3",

        "MEM_LOAD_UOPS_RETIRED.L1_MISS",
        "MEM_LOAD_UOPS_RETIRED.L2_MISS",
        #"MEM_LOAD_UOPS_RETIRED.L3_MISS",

        #"LONGEST_LAT_CACHE.MISS",
        "DTLB-LOADS",
        #"ICACHE.MISSES",
        #"DTLB-LOAD-MISSES",
        "FRONTEND_RETIRED.L1I_MISS",
        # "PERF_COUNT_HW_CACHE_L1I:READ:ACCESS",
        "PERF_COUNT_HW_CACHE_L1I:READ:MISS",
        "ICACHE_64B.IFTAG_MISS",
        "ICACHE_64B.IFTAG_HIT",
        #"L1-ICACHE-PREFETCHES",
        #"L1-ICACHE-PREFETCH-MISSES",
        #"PERF_COUNT_HW_CACHE_L1I:PREFETCH",

        "FRONTEND_RETIRED.L2_MISS",
        "FRONTEND_RETIRED.ITLB_MISS",

        "CYCLE_ACTIVITY.CYCLES_L1D_MISS",
        "CYCLE_ACTIVITY.CYCLES_L2_MISS",
        "CYCLE_ACTIVITY.CYCLES_L3_MISS",

        "CYCLE_ACTIVITY.STALLS_TOTAL",
        "CYCLE_ACTIVITY.STALLS_MEM_ANY",
])

SKYLAKE_EVENTS = ",".join([
        ## FRONT-END STALLS

        # Number of instr. that experienced ITLB miss
        "FRONTEND_RETIRED.ITLB_MISS",

        # Branch MPKI
        "BR_MISP_RETIRED.ALL_BRANCHES",

        # L1I MPKI
        "FRONTEND_RETIRED.L1I_MISS",

        ## BACK-END STALLS

        # L1D MPKI
        "MEM_LOAD_RETIRED.L1_MISS", # Load uop misses
        "L2_RQSTS.ALL_RFO", # RFO requests to L2

        # L2 MPKI
        "MEM_LOAD_RETIRED.L2_MISS",
        # OFFCORE_REQUEST means requests arising from L2 misses. Refer
        # to Intel 64 and IA-32 Architectures Optimization Reference Manual
        # Section B.4.3.5
        "OFFCORE_REQUESTS.DEMAND_RFO",

        # DTLB misses
        "DTLB_LOAD_MISSES.STLB_HIT",
        "DTLB_LOAD_MISSES.MISS_CAUSES_A_WALK",
        "DTLB_STORE_MISSES.STLB_HIT",
        "DTLB_STORE_MISSES.MISS_CAUSES_A_WALK",

        # Cycles breakdown for front-end, back-end
        "CYCLE_ACTIVITY.CYCLES_L1D_MISS",
        "CYCLE_ACTIVITY.CYCLES_L2_MISS",
        "CYCLE_ACTIVITY.CYCLES_L3_MISS",

        "CYCLE_ACTIVITY.STALLS_TOTAL",
        "CYCLE_ACTIVITY.STALLS_MEM_ANY",
])

BROADWELL_EVENTS = ",".join([
        ## FRONT-END STALLS

        # Branch MPKI
        "BR_MISP_RETIRED.ALL_BRANCHES",

        # L1I MPKI
        "ICACHE.MISSES",

        # Number of instr. that experienced ITLB miss
        "ITLB_MISSES.MISS_CAUSES_A_WALK",
        "ITLB_MISSES.STLB_HIT",

        ## BACK-END STALLS

        # L1D MPKI
        "MEM_LOAD_UOPS_RETIRED.L1_MISS", # Load uop misses
        "L2_TRANS.RFO", # RFO requests to L2

        # L2 MPKI
        "MEM_LOAD_UOPS_RETIRED.L2_MISS",
        # OFFCORE_REQUEST means requests arising from L2 misses. Refer
        # to Intel 64 and IA-32 Architectures Optimization Reference Manual
        # Section B.4.3.5
        "OFFCORE_REQUESTS.DEMAND_RFO",

        # DTLB misses
        "DTLB_LOAD_MISSES.STLB_HIT",
        "DTLB_LOAD_MISSES.MISS_CAUSES_A_WALK",
        "DTLB_STORE_MISSES.STLB_HIT",
        "DTLB_STORE_MISSES.MISS_CAUSES_A_WALK",

        # ROB-stalls
        "RESOURCE_STALLS.ROB",
])


