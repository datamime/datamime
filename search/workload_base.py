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

import numpy as np
import pandas as pd
import subprocess
import psutil
import time
import sys
import os
import logging
import math
import yaml
from utils import *
from cost_model import *
from abc import ABC, abstractmethod

# Abstract base class which each new app added should inherit.
class Workload(ABC):
    def __init__(self, uarch, wltype, nthreads, target_profile_dir, results_dir,
                incfg, logger, onlyeval = False, user='root', phases=5500,
                mrc_period=10000, mrc_phases=5500,
                ipc_target = -1, mpki_target = -1):
        # General configuration
        self.user = user
        self.uarch = uarch
        self.wltype = wltype
        self.nthreads = nthreads
        self.target_profile_dir = target_profile_dir
        self.results_dir = results_dir
        self.incfg = incfg
        self.profiled_tids = []
        self.per_thr_cost_logs = []
        self.runs = 0
        self.phases = phases
        self.mrc_period = mrc_period
        self.mrc_phases = mrc_phases
        self.logger = logger

        assert(not (ipc_target >= 0 and mpki_target >= 0))
        self.ipc_target = ipc_target
        self.mpki_target = mpki_target
        self.measure_bbox = True if (ipc_target >= 0 or mpki_target >= 0) else False
        if self.ipc_target >= 0:
            self.ipc_file = open(os.path.join(results_dir, "ipc.out"), 'w')
        elif self.mpki_target >= 0:
            self.mpki_file = open(os.path.join(results_dir, "mpki.out"), 'w')

        # Detect numa topology
        numactl = subprocess.Popen(['numactl', '-H'], encoding='UTF-8',
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout_data, _ = numactl.communicate()
        fl = stdout_data.split("\n")[0]
        sl = stdout_data.split("\n")[1]
        nns = fl[fl.find("(")+1:fl.find(")")]
        self.node0_procs = sl[sl.find(":")+1:].strip().split(" ")
        self.numa_nodes = []
        if '-' in nns:
            self.numa_nodes = list(range(int(nns[0]), int(nns[2]) + 1))
        else:
            self.numa_nodes = [0]

        # Relevant directories provided by user (scratch, apps, data dirs)
        self.scratch_dir = incfg['global']['scratch_dir']
        self.apps_root = incfg['global']['apps_root']
        self.data_root = incfg['global']['data_root']
        self.pythonpath = incfg['global']['pythonpath']

        # Server and client workload binaries and their arguments
        self.server_bin = os.path.join(self.apps_root, incfg[wltype]['server_binpath'])
        self.client_bin = os.path.join(self.apps_root, incfg[wltype]['client_binpath'])
        print(self.server_bin)

        # Server and client domains for remote client setup
        if "server_domain" in incfg[wltype].keys():
            self.server_domain = incfg[wltype]['server_domain']
        if "remote_client_domain" in incfg[wltype].keys():
            self.remote_client_domain = incfg[wltype]['remote_client_domain']
        if "remote_client_binpath" in incfg[wltype].keys():
            self.remote_client_binpath = incfg[wltype]['remote_client_binpath']

        # target_configs.yml is currently needed to specify the tsc frequency of the
        # machien that the target profile was generated from.
        # TODO: If there isn't a need for anything else, just make it an option to pass
        # the tsc frequency.
        # Ignore if using the Workload object just to evaluate a single dataset.
        self.target_tsc_freq = None
        if not target_profile_dir == None:
            target_configs = os.path.join(target_profile_dir, "target_configs.yml")
            if not os.path.exists(target_configs):
                sys.exit("target_configs.yml not present in the target directory.")
            else:
                with open(target_configs, "r") as tcfg_file:
                    try:
                        tcfg = yaml.safe_load(tcfg_file)
                    except yaml.YAMLError as exc:
                        self.logger.info(exc)
                        sys.exit(1)

                    self.target_tsc_freq = int(tcfg["tsc_freq_mhz"])
                    self.logger.info("Target TSC Frequency = {}Mhz".format(self.target_tsc_freq))

        self.tsc_freq = int(incfg['global']['tsc_freq_mhz'])

        # Output-related
        self.params = {}
        self.costs = {}
        self.params_out = None
        self.cost_out = None
        if not onlyeval:
            self.params_out = open(os.path.join(results_dir,"params.out"), 'w')
            self.cost_out = open(os.path.join(results_dir,"cost.out"), 'w')

        # Target statistics. Only need to read once per workload.
        # Make sure to put in correct value for target architecture TSC freq!
        if self.target_profile_dir == None or self.measure_bbox:
            # This is for using the Workload object just for evaluating a parameter.
            self.all_tctrs = None
            self.all_tmrcs = None
            self.all_tipcs = None
            self.all_tstats = None
        else:
            all_tctrs, all_tmrcs, all_tipcs = read_multiple_targets(self.target_profile_dir)
            self.all_tctrs = all_tctrs
            self.all_tmrcs = all_tmrcs
            self.all_tipcs = all_tipcs
            self.all_tstats = []
            for tctrs, tmrcs, tipcs in zip(all_tctrs, all_tmrcs, all_tipcs):
                self.all_tstats.append(generate_stats(tctrs, tmrcs, tipcs, self.uarch,
                self.target_tsc_freq))

    # Takes as input params, which is a dictionary consisting of the parameter name
    # as the key and its value
    @abstractmethod
    def run(self, params, header):
        pass

    def cost(self, all_ctrs, all_mrcs, all_ipcs):
        # Create per-thread cost logs if we measure more than one thread.
        if len(all_ctrs.keys()) > 1 and len(self.per_thr_cost_logs) == 0:
            for tidx in range(len(all_ctrs.keys())):
                thr_cost_out = open(os.path.join(self.results_dir,"thr{}_cost.out".format(tidx)), 'w')
                self.per_thr_cost_logs.append(thr_cost_out)

        # The order of threads profiled for the target MUST match the order
        # profiled by a Workload object.
        tidx = 0
        summed_cost = {}
        avg_ipcs = []
        avg_mpkis = []
        for (ctrs_tid, ctrs), (mrcs_tid, mrcs), (ipcs_tid, ipcs) \
            in zip(all_ctrs.items(), all_mrcs.items(), all_ipcs.items()):

            assert(ctrs_tid == mrcs_tid and mrcs_tid == ipcs_tid)

            avg_ipc, avg_mtpki, ipc_cost, mpki_cost = 0, 0, 0, 0
            if self.measure_bbox:
                stats = generate_avgs(ctrs)
                if self.ipc_target >= 0:
                    avg_ipc = stats['avg_ipc']
                    ipc_cost = abs(self.ipc_target - avg_ipc)
                    if 'avg_ipc' not in summed_cost.keys():
                        summed_cost['avg_ipc'] = 0
                    summed_cost['avg_ipc'] += ipc_cost
                    avg_ipcs.append(avg_ipc)
                else:
                    avg_mtpki = stats['avg_mtpki']
                    mpki_cost = abs(self.mpki_target - avg_mtpki)
                    if 'avg_mtpki' not in summed_cost.keys():
                        summed_cost['avg_mtpki'] = 0
                    summed_cost['avg_mtpki'] += mpki_cost
                    avg_mpkis.append(avg_mtpki)

                if 'total_cost' not in summed_cost.keys():
                    summed_cost['total_cost'] = 0
                summed_cost['total_cost'] += ipc_cost + mpki_cost

            else:
                print("TIDx = {}".format(tidx))
                tstats = self.all_tstats[tidx]
                tctrs = self.all_tctrs[tidx]
                tmrcs = self.all_tmrcs[tidx]
                tipcs = self.all_tipcs[tidx]
                stats = generate_stats(ctrs, mrcs, ipcs, self.uarch, self.tsc_freq)
                subcost = calculate_cost(tstats, tctrs, tmrcs, tipcs, stats, ctrs, mrcs, ipcs)

                # Re-calculate total cost with user-provided weights
                weights = {
                    'ci': self.incfg['global']['weights'][0],
                    'l1i_mpki': self.incfg['global']['weights'][1],
                    'l1d_mpki': self.incfg['global']['weights'][2],
                    'l2_mpki': self.incfg['global']['weights'][3],
                    'br_mpki': self.incfg['global']['weights'][4],
                    'itlb_mpki': self.incfg['global']['weights'][5],
                    'dtlb_mpki': self.incfg['global']['weights'][6],
                    'loc_membw': self.incfg['global']['weights'][7],
                    'mrc': self.incfg['global']['weights'][8],
                    'ipc': self.incfg['global']['weights'][9],
                }

                total_cost = 0;
                skip = ['total_cost']
                for sc_type, sc in subcost.items():
                    if math.isnan(sc):
                        self.logger.info("NAN value for {}!".format(sc_type))
                    elif sc_type in skip:
                        self.logger.info("Skipping {}".format(sc_type))
                    else:
                        total_cost += sc * weights[sc_type]
                subcost['total_cost'] = total_cost

                if len(self.per_thr_cost_logs) > 1:
                    subcost_out = self.per_thr_cost_logs[tidx]
                    tidx += 1
                    sc_name_str = "Costs:"
                    sc_val_str = ""
                    for sc_name, sc_val in subcost.items():
                        sc_name_str = sc_name_str + sc_name + ","
                        sc_val_str = sc_val_str + "{:.4f}".format(sc_val) + ","

                    if self.runs == 1:
                        subcost_out.write(sc_name_str[:len(sc_name_str)-1] + "\n")
                    subcost_out.write(sc_val_str[:len(sc_val_str)-1] + "\n")
                    subcost_out.flush()

                # Accumulate for final logging
                for sc_type, sc in subcost.items():
                    if sc_type in summed_cost.keys():
                        summed_cost[sc_type] += sc
                    else:
                        summed_cost[sc_type] = sc

        # Put total summed cost across all measured threads in a separate
        # output file for backwards compatibility and easy of plotting.
        sc_name_str = "Costs:"
        sc_val_str = ""
        for sc_name, sc_val in summed_cost.items():
            sc_name_str = sc_name_str + sc_name + ","
            sc_val_str = sc_val_str + "{:.4f}".format(sc_val) + ","
        self.logger.info(sc_name_str)
        self.logger.info(sc_val_str)
        self.logger.info("Total cost = {}".format(summed_cost['total_cost']))
        if self.runs == 1:
            self.cost_out.write(sc_name_str[:len(sc_name_str)-1] + "\n")
        self.cost_out.write(sc_val_str[:len(sc_val_str)-1] + "\n")
        self.cost_out.flush()

        self.costs[self.runs-1] = summed_cost

        # Output ipc/mpki measurements to separate file if doing bbox measurement
        if self.ipc_target >= 0:
            ipc_str = ""
            for ipc in avg_ipcs:
                ipc_str = ipc_str + "{:.4f}".format(ipc) + ","
            self.ipc_file.write(ipc_str[:len(ipc_str)-1] + "\n")
            self.ipc_file.flush()
        elif self.mpki_target >= 0:
            mpki_str = ""
            for mpki in avg_mpkis:
                mpki_str = mpki_str + "{:.4f}".format(mpki) + ","
            self.mpki_file.write(mpki_str[:len(mpki_str)-1] + "\n")
            self.mpki_file.flush()

        return summed_cost['total_cost']

    # Read profile for profile generated at _run iteration
    def read_profile(self, _run):
        all_ctrs, all_mrcs, all_ipcs = {}, {}, {} # Ctrs, mrcs, ipcs for each profiled tid
        for tid in self.profiled_tids:
            grouped_ctrspath = os.path.join(self.rawdata_dir, "{}_grouped_counters_{}.csv".format(str(_run), tid))
            mrcspath = os.path.join(self.rawdata_dir, "{}_mrc_{}".format(str(_run), tid))
            ipcspath = os.path.join(self.rawdata_dir, "{}_ipc_{}".format(str(_run), tid))

            all_ctrs[tid] = pd.read_csv(grouped_ctrspath)
            if not self.measure_bbox:
                all_mrcs[tid] = np.loadtxt(mrcspath, unpack=True)
                all_ipcs[tid] = np.loadtxt(ipcspath, unpack=True)
            else:
                all_mrcs[tid] = None
                all_ipcs[tid] = None

        return (all_ctrs, all_mrcs, all_ipcs)

