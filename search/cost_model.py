# $lic$
# Copyright (c) 2021-2022 by Massachusetts Institute of Technology
#
# This file is part of Datamime.
#
# This tool is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation, version 3.
#
# If you use this software in your research, we request that you reference
# the Datamime paper ("Datamime: Generating Representative Benchmarks by
# Automatically Synthesizing Datasets", Lee and Sanchez, MICRO-55, October 2022)
# as the source in any publications that use this software, and that you send
# us a citation of your work.
#
# This tool is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program. If not, see <http://www.gnu.org/licenses/>.
# $endlic$

import re
import os
import sys
import yaml
import math
import numpy as np
import pandas as pd
from scipy.stats import wasserstein_distance
from decimal import Decimal
import itertools

from utils import *

def statistical_distance(u, v):
    return wasserstein_distance(u, v)

# Generate relevant statistics given samples
# Define the relevant profiles you want here!
# Needs architecture as a parameter since different architectures use different
# counters for measuring relevant profiles...
def generate_stats(ctrs, mrcs, ipcs, arch="skylake", tsc_freq_mhz = 3400):

    # Average statistics available to all uarchs
    ci = (ctrs["UNHALTED_REFERENCE_CYCLES"]
        / ctrs["ref_cycles"]).to_numpy()
    avg_ipc = np.sum(ctrs["INST_RETIRED"].to_numpy()) / np.sum(ctrs["CPU_CLK_UNHALTED"].to_numpy())
    avg_ci = np.sum(ctrs["UNHALTED_REFERENCE_CYCLES"].to_numpy()[1:]) \
        / np.sum(ctrs["ref_cycles"].to_numpy()[1:]) # Discard first sample as it is erronous

    # Pure IPC and MTPKI distributions at 12 ways
    ipc_dist = (ctrs["INST_RETIRED"] / ctrs["CPU_CLK_UNHALTED"]).to_numpy()[1:]

    ## ADD YOUR NEW ARCHITECTURE DEFINITION HERE
    # Since each processor architecture has different sets of performance counters,
    # we need to have a per-architecture definition of which performance counters
    # are used to generate some of the metrics used by Datamime (br_mpki, l1i_mpki,
    # l1d_mpki, l2_mpki, itlb_mpki, dtlb_mpki)
    if arch == "skylake":
        br_mpki = filter_nan((1000 * ctrs["BR_MISP_RETIRED.ALL_BRANCHES"]
            / ctrs["INST_RETIRED"]).to_numpy())
        l1i_mpki = filter_nan((1000 * ctrs["FRONTEND_RETIRED.L1I_MISS"]
            / ctrs["INST_RETIRED"]).to_numpy())
        itlb_mpki = filter_nan((1000 * ctrs["FRONTEND_RETIRED.ITLB_MISS"]
            / ctrs["INST_RETIRED"]).to_numpy())

        # Profiles that are accumulated from multiple counters inidividually
        # need to be filtered before summing since different counters may
        # belong to different groups
        ld_l1d_mpki = filter_nan(1000 * (ctrs["MEM_LOAD_RETIRED.L1_MISS"]
            / ctrs["INST_RETIRED"]).to_numpy())
        st_l1d_mpki = filter_nan(1000 * (ctrs["L2_RQSTS.ALL_RFO"]
            / ctrs["INST_RETIRED"]).to_numpy())
        l1d_mpki = truncate_and_add(ld_l1d_mpki, st_l1d_mpki)

        ld_l2_mpki = filter_nan(1000 * (ctrs["MEM_LOAD_RETIRED.L2_MISS"]
            / ctrs["INST_RETIRED"]).to_numpy())
        st_l2_mpki = filter_nan(1000 * (ctrs["OFFCORE_REQUESTS.DEMAND_RFO"]
            / ctrs["INST_RETIRED"]).to_numpy())
        l2_mpki = truncate_and_add(ld_l2_mpki, st_l2_mpki)

        ld_s_hit = filter_nan(1000 * (ctrs["DTLB_LOAD_MISSES.STLB_HIT"]
            / ctrs["INST_RETIRED"]).to_numpy())
        ld_s_miss = filter_nan(1000 * (ctrs["DTLB_LOAD_MISSES.MISS_CAUSES_A_WALK"]
            / ctrs["INST_RETIRED"]).to_numpy())
        st_s_hit = filter_nan(1000 * (ctrs["DTLB_STORE_MISSES.STLB_HIT"]
            / ctrs["INST_RETIRED"]).to_numpy())
        st_s_miss = filter_nan(1000 * (ctrs["DTLB_STORE_MISSES.MISS_CAUSES_A_WALK"]
            / ctrs["INST_RETIRED"]).to_numpy())
        ld_stats = truncate_and_add(ld_s_hit, ld_s_miss)
        st_stats = truncate_and_add(st_s_hit, st_s_miss)
        dtlb_mpki = truncate_and_add(ld_stats, st_stats)

        # BW in GB/s
        # FIXME: TSC frequency should be modifiable.
        loc_membw = (ctrs["local_mem_traffic"] * tsc_freq_mhz * 1e6 / ctrs["ref_cycles"]).to_numpy()
    elif arch == "broadwell":
        br_mpki = filter_nan((1000 * ctrs["BR_MISP_RETIRED.ALL_BRANCHES"]
            / ctrs["INST_RETIRED"]).to_numpy())
        l1i_mpki = filter_nan((1000 * ctrs["ICACHE.MISSES"]
            / ctrs["INST_RETIRED"]).to_numpy())

        # Profiles that are accumulated from multiple counters inidividually
        # need to be filtered before summing since different counters may
        # belong to different groups
        ld_l1d_mpki = filter_nan(1000 * (ctrs["MEM_LOAD_UOPS_RETIRED.L1_MISS"]
            / ctrs["INST_RETIRED"]).to_numpy())
        st_l1d_mpki = filter_nan(1000 * (ctrs["L2_TRANS.RFO"]
            / ctrs["INST_RETIRED"]).to_numpy())
        l1d_mpki = truncate_and_add(ld_l1d_mpki, st_l1d_mpki)

        ld_l2_mpki = filter_nan(1000 * (ctrs["MEM_LOAD_UOPS_RETIRED.L2_MISS"]
            / ctrs["INST_RETIRED"]).to_numpy())
        st_l2_mpki = filter_nan(1000 * (ctrs["OFFCORE_REQUESTS.DEMAND_RFO"]
            / ctrs["INST_RETIRED"]).to_numpy())
        l2_mpki = truncate_and_add(ld_l2_mpki, st_l2_mpki)

        i_hit = filter_nan(1000 * (ctrs["ITLB_MISSES.STLB_HIT"]
            / ctrs["INST_RETIRED"]).to_numpy())
        i_miss = filter_nan(1000 * (ctrs["ITLB_MISSES.MISS_CAUSES_A_WALK"]
            / ctrs["INST_RETIRED"]).to_numpy())
        itlb_mpki = truncate_and_add(i_hit, i_miss)

        ld_s_hit = filter_nan(1000 * (ctrs["DTLB_LOAD_MISSES.STLB_HIT"]
            / ctrs["INST_RETIRED"]).to_numpy())
        ld_s_miss = filter_nan(1000 * (ctrs["DTLB_LOAD_MISSES.MISS_CAUSES_A_WALK"]
            / ctrs["INST_RETIRED"]).to_numpy())
        st_s_hit = filter_nan(1000 * (ctrs["DTLB_STORE_MISSES.STLB_HIT"]
            / ctrs["INST_RETIRED"]).to_numpy())
        st_s_miss = filter_nan(1000 * (ctrs["DTLB_STORE_MISSES.MISS_CAUSES_A_WALK"]
            / ctrs["INST_RETIRED"]).to_numpy())
        ld_stats = truncate_and_add(ld_s_hit, ld_s_miss)
        st_stats = truncate_and_add(st_s_hit, st_s_miss)
        dtlb_mpki = truncate_and_add(ld_stats, st_stats)

        # BW in GB/s
        # FIXME: TSC frequency should be modifiable.
        loc_membw = (ctrs["local_mem_traffic"] * tsc_freq_mhz * 1e6 / ctrs["ref_cycles"]).to_numpy()
    elif arch == "skylake-old":
        br_mpki = (1000 * ctrs["BR_MISP_RETIRED"] / ctrs["INST_RETIRED"]).to_numpy()
        l1i_mpki = (1000 * ctrs["FRONTEND_RETIRED.L1I_MISS"]
            / ctrs["INST_RETIRED"]).to_numpy()
        l1d_mpki = (1000 * ctrs["MEM_LOAD_UOPS_RETIRED.L1_MISS"]
            / ctrs["INST_RETIRED"]).to_numpy()
        l2_mpki = (1000 * ctrs["MEM_LOAD_UOPS_RETIRED.L2_MISS"]
            / ctrs["INST_RETIRED"]).to_numpy()
        itlb_mpki = (1000 * ctrs["FRONTEND_RETIRED.ITLB_MISS"]
            / ctrs["INST_RETIRED"]).to_numpy()
        # hrlee: incorrect counter -- should not be used...
        dtlb_mpki = (1000 * ctrs["DTLB-LOADS"]
            / ctrs["INST_RETIRED"]).to_numpy()
    else:
        sys.exit("Incorrect arch: {}".format(arch))

    # Remove zeros from counters that are time-multiplexed
    #l1i_mpki = l1i_mpki.reshape(-1,1)
    #l1d_mpki = l1d_mpki.reshape(-1,1)
    #l2_mpki = l2_mpki.reshape(-1,1)
    #br_mpki = br_mpki.reshape(-1,1)
    #itlb_mpki = itlb_mpki.reshape(-1,1)
    #dtlb_mpki = dtlb_mpki.reshape(-1,1)
    #membw = membw.reshape(-1,1)

    stats = {}

    stats['ci'] = ci
    stats['l1i_mpki'] = l1i_mpki
    stats['l1d_mpki'] = l1d_mpki
    stats['l2_mpki'] = l2_mpki
    stats['br_mpki'] = br_mpki
    stats['itlb_mpki'] = itlb_mpki
    stats['dtlb_mpki'] = dtlb_mpki
    stats['loc_membw'] = loc_membw
    stats['avg_ipc'] = avg_ipc
    #stats['avg_llc_mpki'] = avg_llc_mpki
    stats['avg_ci'] = avg_ci
    stats['ipc_dist'] = ipc_dist

    # Now work on 2-D data (miss/ipc curve)
    # (hrlee): If all values are zero, statistics are meaningless.
    # TODO: Are there other conditions where if some values are zero the statistics
    # are again uncalculatable?
    if np.any(mrcs) == True and np.any(ipcs) == True:
        mrc_means = np.mean(mrcs, axis=0)
        ipc_means = np.mean(ipcs, axis=0)
        mrc_maxs = np.max(mrcs, axis=0)
        mrc_mins = np.min(mrcs, axis=0)
        ipc_maxs = np.max(ipcs, axis=0)
        ipc_mins = np.min(ipcs, axis=0)

        # reshaping to allow broadcasts
        mrc_maxs = mrc_maxs.reshape(mrc_maxs.shape[0], 1)
        mrc_mins = mrc_mins.reshape(mrc_maxs.shape[0], 1)
        ipc_maxs = ipc_maxs.reshape(ipc_maxs.shape[0], 1)
        ipc_mins = ipc_mins.reshape(ipc_maxs.shape[0], 1)

        stats['mrc_means'] = mrc_means
        stats['mrc_maxs'] = mrc_maxs
        stats['mrc_mins'] = mrc_mins

        stats['ipc_means'] = ipc_means
        stats['ipc_maxs'] = ipc_maxs
        stats['ipc_mins'] = ipc_mins
    else:
        print("Skipping stats for mrcs and ipcs")

    return stats


# Generate relevant average statistics given counter samples
def generate_avgs(ctrs, tsc_freq_mhz = 2000):
    avg_ipc = np.sum(ctrs["INST_RETIRED"].to_numpy()) \
        / np.sum(ctrs["CPU_CLK_UNHALTED"].to_numpy())

    avg_mtpki = 1000 * np.sum(ctrs["local_mem_traffic"].to_numpy()[1:]) \
        / (64 * np.sum(ctrs["INST_RETIRED"].to_numpy()[1:]))

    bm_ctrs = ctrs.dropna(subset=['BR_MISP_RETIRED.ALL_BRANCHES'])
    avg_br_mpki = 1000 * np.sum(bm_ctrs["BR_MISP_RETIRED.ALL_BRANCHES"].to_numpy()) \
        / np.sum(bm_ctrs["INST_RETIRED"].to_numpy())

    im_ctrs = ctrs.dropna(subset=['ICACHE.MISSES'])
    avg_l1i_mpki = 1000 * np.sum(im_ctrs["ICACHE.MISSES"].to_numpy()) \
        / np.sum(im_ctrs["INST_RETIRED"].to_numpy())

    avg_ci = np.sum(ctrs["UNHALTED_REFERENCE_CYCLES"].to_numpy()[1:]) \
        / np.sum(ctrs["ref_cycles"].to_numpy()[1:]) # Discard first sample as it is erronous


    ld_l1d_ctrs = ctrs.dropna(subset=["MEM_LOAD_UOPS_RETIRED.L1_MISS"])
    st_l1d_ctrs = ctrs.dropna(subset=["L2_TRANS.RFO"])
    inst_retired = gen_inst_retired(ld_l1d_ctrs, st_l1d_ctrs)
    avg_l1d_mpki = 1000 * ( \
            np.sum(ld_l1d_ctrs["MEM_LOAD_UOPS_RETIRED.L1_MISS"].to_numpy()) + \
            np.sum(st_l1d_ctrs["L2_TRANS.RFO"].to_numpy()) \
        ) / inst_retired

    ld_l2_ctrs = ctrs.dropna(subset=["MEM_LOAD_UOPS_RETIRED.L2_MISS"])
    st_l2_ctrs = ctrs.dropna(subset=["OFFCORE_REQUESTS.DEMAND_RFO"])
    inst_retired = gen_inst_retired(ld_l2_ctrs, st_l2_ctrs)
    avg_l2_mpki = 1000 * ( \
            np.sum(ld_l2_ctrs["MEM_LOAD_UOPS_RETIRED.L2_MISS"].to_numpy()) + \
            np.sum(st_l2_ctrs["OFFCORE_REQUESTS.DEMAND_RFO"].to_numpy()) \
        ) / inst_retired

    i_hit_ctrs = ctrs.dropna(subset=["ITLB_MISSES.STLB_HIT"])
    i_miss_ctrs = ctrs.dropna(subset=["ITLB_MISSES.MISS_CAUSES_A_WALK"])
    inst_retired = gen_inst_retired(i_hit_ctrs, i_miss_ctrs)
    avg_itlb_mpki = 1000 * ( \
            np.sum(i_hit_ctrs["ITLB_MISSES.STLB_HIT"].to_numpy()) + \
            np.sum(i_miss_ctrs["ITLB_MISSES.MISS_CAUSES_A_WALK"].to_numpy()) \
        ) / inst_retired

    ld_d_hit_ctrs  = ctrs.dropna(subset=["DTLB_LOAD_MISSES.STLB_HIT"])
    ld_d_miss_ctrs = ctrs.dropna(subset=["DTLB_LOAD_MISSES.MISS_CAUSES_A_WALK"])
    st_d_hit_ctrs  = ctrs.dropna(subset=["DTLB_STORE_MISSES.STLB_HIT"])
    st_d_miss_ctrs = ctrs.dropna(subset=["DTLB_STORE_MISSES.MISS_CAUSES_A_WALK"])
    inst_retired = gen_inst_retired(ld_d_hit_ctrs, ld_d_miss_ctrs,
                                    st_d_hit_ctrs, st_d_miss_ctrs)
    avg_dtlb_mpki = 1000 * ( \
            np.sum(ld_d_hit_ctrs["DTLB_LOAD_MISSES.STLB_HIT"].to_numpy()) + \
            np.sum(ld_d_miss_ctrs["DTLB_LOAD_MISSES.MISS_CAUSES_A_WALK"].to_numpy()) + \
            np.sum(st_d_hit_ctrs["DTLB_STORE_MISSES.STLB_HIT"].to_numpy()) + \
            np.sum(st_d_miss_ctrs["DTLB_STORE_MISSES.MISS_CAUSES_A_WALK"].to_numpy()) \
        ) / inst_retired

    avg_loc_membw = np.sum(ctrs["local_mem_traffic"].to_numpy()[1:]) * tsc_freq_mhz * 1e6 \
            / np.sum(ctrs["ref_cycles"].to_numpy()[1:])

    stats = {}
    stats['avg_ci']        = avg_ci    # Compute Intensity
    stats['avg_ipc']       = avg_ipc   # IPC
    stats['avg_mtpki']     = avg_mtpki # Memory traffic / Kilo-instructions
    stats['avg_br_mpki']   = avg_br_mpki # Branch misses
    stats['avg_l1i_mpki']  = avg_l1i_mpki # Instruction misses
    stats['avg_l1d_mpki']  = avg_l1d_mpki # L1D misses
    stats['avg_l2_mpki']   = avg_l2_mpki  # L2 misses
    stats['avg_itlb_mpki'] = avg_itlb_mpki # Instruction TLB misses
    stats['avg_dtlb_mpki'] = avg_dtlb_mpki # Data TLB misses
    stats['avg_loc_membw'] = avg_loc_membw # Average local membw usage

    return stats


# Calculate the EMD cost of
def calculate_cost(tstats, tctrs, tmrcs, tipcs,
                   stats, ctrs, mrcs, ipcs) -> dict:

    # ========== Read target profile statistics ===========================
    tci = tstats['ci']
    tl1i_mpki = tstats['l1i_mpki']
    tl1d_mpki = tstats['l1d_mpki']
    tl2_mpki = tstats['l2_mpki']
    tbr_mpki = tstats['br_mpki']
    titlb_mpki = tstats['itlb_mpki']
    tdtlb_mpki = tstats['dtlb_mpki']
    tloc_membw = tstats['loc_membw']

    tmrc_means = tstats['mrc_means']
    tmrc_maxs = tstats['mrc_maxs']
    tmrc_mins = tstats['mrc_mins']
    tipc_means = tstats['ipc_means']
    tipc_maxs = tstats['ipc_maxs']
    tipc_mins = tstats['ipc_mins']

    # ========== Generate empirical profile statistics ========================
    ci = stats['ci']
    l1i_mpki = stats['l1i_mpki']
    l1d_mpki = stats['l1d_mpki']
    l2_mpki = stats['l2_mpki']
    br_mpki = stats['br_mpki']
    itlb_mpki = stats['itlb_mpki']
    dtlb_mpki = stats['dtlb_mpki']
    loc_membw = stats['loc_membw']

    mrc_means = stats['mrc_means']
    mrc_maxs = stats['mrc_maxs']
    mrc_mins = stats['mrc_mins']
    ipc_means = stats['ipc_means']
    ipc_maxs = stats['ipc_maxs']
    ipc_mins = stats['ipc_mins']

    # =========== Calculate per-metric cost ===================================

    subcost = {}
    subcost['ci'] = 0
    subcost['l1i_mpki'] = 0
    subcost['l1d_mpki'] = 0
    subcost['l2_mpki'] = 0
    subcost['br_mpki'] = 0
    subcost['itlb_mpki'] = 0
    subcost['dtlb_mpki'] = 0
    subcost['loc_membw'] = 0
    subcost['mrc'] = 0
    subcost['ipc'] = 0

    # Assume uniform weights
    weights = {
        'ci': 1,
        'l1i_mpki':1,
        'l1d_mpki':1,
        'l2_mpki': 1,
        'br_mpki': 1,
        'itlb_mpki':1,
        'dtlb_mpki':1,
        'loc_membw':1,
        'mrc': 1,
        'ipc': 1
    }

    # Let's try min-max scaling
    # Choose the overall maximum/minimum between the target and the empirical
    # so that all values from the target and empirical lie between [0, 1]

    # First, calculate for 1-D data (pmu counters)
    ci_normed, tci_normed               = normalize_samples(ci, tci)
    l1i_mpki_normed, tl1i_mpki_normed   = normalize_samples(l1i_mpki, tl1i_mpki)
    l1d_mpki_normed, tl1d_mpki_normed   = normalize_samples(l1d_mpki, tl1d_mpki)
    l2_mpki_normed, tl2_mpki_normed     = normalize_samples(l2_mpki, tl2_mpki)
    br_mpki_normed, tbr_mpki_normed     = normalize_samples(br_mpki, tbr_mpki)
    itlb_mpki_normed, titlb_mpki_normed = normalize_samples(itlb_mpki, titlb_mpki)
    dtlb_mpki_normed, tdtlb_mpki_normed = normalize_samples(dtlb_mpki, tdtlb_mpki)
    loc_membw_normed, tloc_membw_normed = normalize_samples(loc_membw, tloc_membw)

    subcost['ci'] = statistical_distance(ci_normed, tci_normed)
    subcost['l1i_mpki'] = statistical_distance(l1i_mpki_normed, tl1i_mpki_normed)
    subcost['l1d_mpki'] = statistical_distance(l1d_mpki_normed, tl1d_mpki_normed)
    subcost['l2_mpki'] = statistical_distance(l2_mpki_normed, tl2_mpki_normed)
    subcost['br_mpki'] = statistical_distance(br_mpki_normed, tbr_mpki_normed)
    subcost['itlb_mpki'] = statistical_distance(itlb_mpki_normed, titlb_mpki_normed)
    subcost['dtlb_mpki'] = statistical_distance(dtlb_mpki_normed, tdtlb_mpki_normed)
    subcost['loc_membw'] = statistical_distance(loc_membw_normed, tloc_membw_normed)

    # Next, 2-D data (miss/ipc curves)
    # Take each slice per way, which gives a 1-D sample. Normalize as before,
    # sum the costs across all ways,
    # then take average along all ways.

    ways = np.shape(mrcs)[1]
    tways = np.shape(tmrcs)[1]
    assert tways == ways == 12 # REMOVE FOR RELEASE
    mrc_cost, ipc_cost = 0, 0
    for w in range(ways):
        mrc_slice = mrcs[:,w]
        ipc_slice = ipcs[:,w]
        tmrc_slice = tmrcs[:,w]
        tipc_slice = tipcs[:,w]

        mrc_slice_normed, tmrc_slice_normed = normalize_samples(mrc_slice, tmrc_slice)
        ipc_slice_normed, tipc_slice_normed = normalize_samples(ipc_slice, tipc_slice)
        mrc_cost += statistical_distance(mrc_slice_normed, tmrc_slice_normed)
        ipc_cost += statistical_distance(ipc_slice_normed, tipc_slice_normed)

    subcost['mrc'] = mrc_cost / ways
    subcost['ipc'] = ipc_cost / ways

    # TODO: Experiment with different cost weights
    total_cost = 0;
    #skip = ["loc_membw"]
    skip = []
    for sc_type, sc in subcost.items():
        if math.isnan(sc):
            print("NAN value for {}!".format(sc_type))
        elif sc_type in skip:
            print("Skipping {}".format(sc_type))
        else:
            total_cost += sc * weights[sc_type]
    subcost['total_cost'] = total_cost

    return subcost

