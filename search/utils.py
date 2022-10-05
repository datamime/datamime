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

# Contains various helper functions to load and manipulate sample data
import re
import os
import sys
import yaml
import math
import numpy as np
import pandas as pd
from decimal import Decimal
import itertools

# Normalize Samples s1 and s2 by the overall maximum of all observed samples.
def normalize_samples(s1, s2):
    omax = max(s1.max(), s2.max())
    s1_normalized = s1 / omax
    s2_normalized = s2 / omax
    return s1_normalized, s2_normalized

# Quantize to second decimal place
def quantize(val):
    return float(Decimal(float(val)).quantize(Decimal('1.00')))

def readcfg(cfg_yml_path):
    if not os.path.exists(cfg_yml_path):
        sys.exit("Config file not present: {}".format(cfg_yml_path))
    else:
        with open(cfg_yml_path, "r") as cfg_file:
            try:
                return yaml.safe_load(cfg_file)
            except yaml.YAMLError as exc:
                print(exc)
                sys.exit(1)

def construct_params(param_labels, params) -> dict:
    params_dict = {}
    for pl, p in zip(param_labels, params):
        params_dict[pl] = p
    return params_dict

def load_costs_params(result_path, truncate=0):
    cost_labels = []
    param_labels = []
    costs = []
    params = []

    # Legacy format output cost and params with the ".log" extension.
    # Cost and param output should now be written with the ".out" extension
    # going forward.
    cost_fn, params_fn = "", ""
    if os.path.exists(os.path.join(result_path, "cost.log")):
        cost_fn = os.path.join(result_path, "cost.log")
    else:
        cost_fn = os.path.join(result_path, "cost.out")
    if os.path.exists(os.path.join(result_path, "params.log")):
        params_fn = os.path.join(result_path, "params.log")
    else:
        params_fn = os.path.join(result_path, "params.out")

    with open(cost_fn, 'r') as cf:
        for idx, c in enumerate(cf.readlines()):
            c = c.strip()
            if idx == 0:
                cost_labels = c.split(",")
            else:
                clist = c.split(",")
                costs.append(clist)

    with open(params_fn, 'r') as pf:
        for idx, p in enumerate(pf.readlines()):
            p = p.strip()
            if idx == 0:
                param_labels = p.split(",")
            else:
                plist = p.split(",")
                params.append(plist)

    costs = np.array(costs).transpose().astype(np.float)
    params = np.array(params).transpose().astype(np.float)

    if truncate > 0:
        costs = costs[:,:truncate]
        params = params[:,:truncate]

    return (cost_labels, costs, param_labels, params)

def get_tid_from_fn(fn):
    tid = 0
    if fn.find("_mrc_") != -1:
        tid = fn[fn.rfind("_mrc_") + len("_mrc") + 1:]
    elif fn.find("_ipc_") != -1:
        tid = fn[fn.rfind("_ipc_") + len("_ipc") + 1:]
    elif fn.find("_grouped_counters_") != -1:
        substr = fn[fn.rfind("_grouped_counters_") + len("_grouped_counters_"):]
        tid = substr[:substr.find(".csv")]
    else:
        raise RuntimeError("Function get_tid_from_fn() did not recieve a valid "
            "filename. Instead received {}".format(fn))

    # Will raise ValueError if tid is not a proper integer
    return int(tid)

# Read a Target that we profiled multiple threads for
def read_multiple_targets(profile_dir_path, check_tid_eq=False, _unpack=True):
    tregex = re.compile('(.*_mrc_.*)|(.*_ipc_.*)|(.*_grouped_.*\.csv)')
    tctrs_files, tmrcs_files, tipcs_files = [], [], []
    for _, _, filenames in os.walk(profile_dir_path):
        for fn in filenames:
            res = tregex.match(fn)
            if res:
                if res.group(1):
                    tmrcs_files.append(fn)
                if res.group(2):
                    tipcs_files.append(fn)
                if res.group(3):
                    tctrs_files.append(fn)

    tctrs_files = sorted(tctrs_files)
    tmrcs_files = sorted(tmrcs_files)
    tipcs_files = sorted(tipcs_files)

    all_tctrs, all_tmrcs, all_tipcs = [], [], []
    for tctrs_fn, tmrcs_fn, tipcs_fn in zip(tctrs_files, tmrcs_files, tipcs_files):
        tctrs_tid = get_tid_from_fn(tctrs_fn)
        tmrcs_tid = get_tid_from_fn(tmrcs_fn)
        tipcs_tid = get_tid_from_fn(tipcs_fn)
        if check_tid_eq:
            assert(tctrs_tid == tmrcs_tid and tmrcs_tid == tipcs_tid)
        else:
            assert(tmrcs_tid == tipcs_tid)

        tctrs = pd.read_csv(os.path.join(profile_dir_path, tctrs_fn))
        tmrcs = np.loadtxt(os.path.join(profile_dir_path, tmrcs_fn), unpack=_unpack)
        tipcs = np.loadtxt(os.path.join(profile_dir_path, tipcs_fn), unpack=_unpack)
        all_tctrs.append(tctrs)
        all_tmrcs.append(tmrcs)
        all_tipcs.append(tipcs)

    return (all_tctrs, all_tmrcs, all_tipcs)

def read_profiles_dir(profile_dir_path, _unpack=True, skip_curves=False):
    tregex = re.compile('(.*_mrc_.*)|(.*_ipc_.*)|(.*_grouped_.*\.csv)')
    tmrc_file = None
    tipc_file = None
    tctrs_file = None
    for _, _, filenames in os.walk(profile_dir_path):
        for fn in filenames:
            res = tregex.match(fn)
            if res:
                if res.group(1):
                    tmrc_file = fn
                if res.group(2):
                    tipc_file = fn
                if res.group(3):
                    tctrs_file = fn
    if skip_curves:
        tmrcs = np.zeros(1)
        tipcs = np.zeros(1)
    else:
        tmrcs = np.loadtxt(os.path.join(profile_dir_path, tmrc_file), unpack=_unpack)
        tipcs = np.loadtxt(os.path.join(profile_dir_path, tipc_file), unpack=_unpack)
    tctrs = pd.read_csv(os.path.join(profile_dir_path, tctrs_file))

    return (tctrs, tmrcs, tipcs)

def truncate_and_add(s1, s2):
    if len(s1) != len(s2):
        s1 = s1[:min(len(s1), len(s2))]
        s2 = s2[:min(len(s1), len(s2))]
    return s1 + s2

def filter_nan(nparray):
    return nparray[~np.isnan(nparray)]

# If the individual counters that aggregate to a metric come from different groups of counters...
# The only reasonable choice here is to average the denominator (instructions retired)
# such that we approximate the average metric.
def gen_inst_retired(*ctrs):
    prevctr = None
    inst_retired_list = []
    for ctr in ctrs:
        inst_retired_list.append(ctr["INST_RETIRED"].to_numpy())

    need_averaging = False
    for ir1, ir2 in itertools.combinations(inst_retired_list, 2):
        if not np.array_equal(ir1, ir2):
            need_averaging = True
            break

    if need_averaging:
        return np.sum(np.mean(np.array(inst_retired_list), axis=0))
    else:
        return np.sum(inst_retired_list[0])

