#!/usr/bin/env python3

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

import sys
import subprocess
import argparse
import random
import signal
import os
import yaml
from to_csv import to_csv
from os.path import join as joinpath
from events import *
import pandas as pd
import time

def error(error_str):
    print(error_str)
    sys.exit()


parser = argparse.ArgumentParser()
parser.add_argument("outfile_header", type=str, help="Header for the output"
    " files of profiler")
parser.add_argument("tscfreq", type=int,
    help="TSC frequency of the machine being used to profile the application")
parser.add_argument("tids", nargs="+", help="Linux thread ids to measure")
parser.add_argument("--mrc", action='store_true', default=False,
    help="Miss and IPC curve profiling mode. When enabled, grouped counters"
    " profiling is disabled.")
parser.add_argument("-p", "--num_phases", type=int, default=5500,
    help="Specify number of phases to profile")
parser.add_argument("-l", "--phase_len", type=int, default=20000000,
    help="Specify profile phase len (in cycles)")
parser.add_argument("-w", "--mrc_warmup_period", type=int, default=1000,
    help="Specify mrc warmup period (in Mcycles)")
parser.add_argument("-m", "--mrc_profile_period", type=int, default=10000,
    help="Specify mrc profile period (in Mcycles)")
parser.add_argument("-d", "--results_dir", type=str,
    help="Directory where you want to store your results")
parser.add_argument("-u", "--uarch", type=str, default="broadwell",
    help="uArch of the machine the profiler is running on (Options: skylake, skylake-old, broadwell). Default is broadwell.")
parser.add_argument("--debug", action="store_true",
    help="Output debug messages to the log file")
parser.add_argument("-a", "--app", type=str, default=None,
    help="Path to binary which the harness will spawn for profiler to profile. Must be single-threaded.")

DEFAULT_RESULTS_DIR = os.path.join(os.getcwd(), "output")

PROFILERPATH = joinpath(os.path.dirname(os.path.abspath(__file__)), "datamime-profiler")

def print_basic_stats(csvname):
    df = pd.read_csv(csvname)

    cpu_intensity = df["UNHALTED_REFERENCE_CYCLES"] / df["ref_cycles"]

    print("cpu intensity: %f" % cpu_intensity.mean())

    ipc = df["INST_RETIRED"] / df["CPU_CLK_UNHALTED"]

    print("ipc: %f" % ipc.mean())


def tid_to_tgid(tid):
    with open("/proc/%s/status" % tid, "r") as statusFile:
        tgid_line = [ line for line in statusFile if line.startswith("Tgid") ][0]
        _, tgid = tgid_line.strip().split("\t")
        return tgid

def profile_threads(uarch, tids, outfile_header, mrc_enabled, results_dir,
    num_phases=5500, phase_len=20000000, mrc_warmup_period=1000,
    mrc_profile_period=10000, debug=False, app=None):

    events = []
    if uarch == "skylake-old":
        events = OLD_EVENTS
    elif uarch == "skylake":
        events = SKYLAKE_EVENTS
    elif uarch == "broadwell":
        events = BROADWELL_EVENTS
    else:
        sys.exit("Unsupported uarch: {}".format(uarch))

    # Spawn app if provided
    if app != None:
        print(f"Launching provided application: {app}")
        app_to_profile = subprocess.Popen([app])
        # Profile this app
        tids = [str(app_to_profile.pid)]

    tgid = tid_to_tgid(tids[0])
    cmd = [ PROFILERPATH,
        "-e" + events, "-l" + str(phase_len), "-n" + str(num_phases),
        "-w" + str(mrc_warmup_period), "-p" + str(mrc_profile_period),
        "-f" + outfile_header, "-g" + tgid, "-r" + results_dir]

    if debug:
        cmd.append("-d")

    if mrc_enabled:
        cmd.append("-m")

    tids_str = "-t"
    for tid in tids:
        tids_str = tids_str + tid + ","
    tids_str = tids_str[:len(tids_str)-1]

    cmd.append(tids_str)

    start = time.time()

    profiler = subprocess.Popen(cmd)
    print("Launching: %s" % " ".join(cmd))
    print("logfile_header = {}".format(outfile_header))
    received_sigint = False
    try:
        profiler.wait()
    except KeyboardInterrupt:
        profiler.wait()
        received_sigint = True

    end = time.time()
    print("Elapsed profiling time = {} seconds".format(end - start))

    if app != None:
        print(f"Terminating provided app: {args.app}")
        app.kill()
        app.wait()

    print("[PROFILER-HARNESS] now creating csvs")
    for tid in tids:
        if os.path.exists(os.path.join(results_dir, outfile_header + "_grouped_counters_" + tid)):
            csvname = to_csv(os.path.join(results_dir, outfile_header + "_grouped_counters_" + tid))
        elif os.path.exists(os.path.join(results_dir, outfile_header + "_counters_" + tid)):
            csvname = to_csv(os.path.join(results_dir, outfile_header + "_counters_" + tid))
        else:
            sys.exit("[PROFILER-HARNESS] No counters dump found. Exiting")

        print_basic_stats(csvname)
    print("[PROFILER-HARNESS] all done")
    return received_sigint

# Allow running this as a script
# TODO: Separate out the profiler method to another module.
if __name__ == "__main__":

    args = parser.parse_args()
    tids = args.tids
    outfile_header = args.outfile_header
    mrc_enabled = args.mrc
    results_dir = args.results_dir if args.results_dir is not None else DEFAULT_RESULTS_DIR

    if not os.path.exists(results_dir):
        os.mkdir(results_dir)

    targetcfg = {"tsc_freq_mhz": args.tscfreq}
    with open(os.path.join(results_dir, "target_configs.yml"), 'w') as targetcfg_file:
        yaml.safe_dump(targetcfg, targetcfg_file, default_flow_style=False)

    profile_threads(args.uarch, tids, outfile_header, mrc_enabled, results_dir,
    num_phases=args.num_phases, phase_len=args.phase_len,
    mrc_warmup_period=args.mrc_warmup_period,
    mrc_profile_period=args.mrc_profile_period,
    debug=args.debug,
    app=args.app)
