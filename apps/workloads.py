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
import sys, os
import logging
import math
from decimal import Decimal
from distribution import *

# This module is always imported from the search_dataset.py module located
# within the Datamime code repo, so use the cwd to point to modules to import
# from there.
sys.path.append(os.getcwd())
sys.path.append(os.path.join(os.getcwd(), "../profiler"))
from workload_base import Workload
from harness import profile_threads

"""
    Implementations of different workloads
    Currently implemented: Memcached, Silo, Xapian, Dnn-As-A-Service
    """
class DnnWorkload(Workload):

    def run(self, params, header):
        server_tidfile = os.path.join(self.scratch_dir, "tbench_server_tid.txt")

        # Clean up the worker thread id file possibly left from previous run.
        if os.path.exists(server_tidfile):
            os.remove(server_tidfile)

        # First, create model and serialize
        cmd = [# Need to run the venv python
               self.pythonpath,
               os.path.join(self.apps_root, 'dnn/scripts/create-model.py'),
               '-c', str(params["conv_layers"]),
               '-t', str(params["strided_conv_layers"]),
               '-m', str(params["maxpool_layers"]),
               '-f', str(params["fc_layers"]),
               '-o', str(params["init_channels"]),
               '-g', '-r',  # Serialize generated model
               '-P', self.scratch_dir # Save created model here
        ]
        self.logger.info(subprocess.list2cmdline(cmd))
        create_model = subprocess.Popen(cmd)
        create_model.wait()

        # Next, run workload with the model
        dnn_env = os.environ.copy()
        dnn_env['SCRATCH_DIR'] = self.scratch_dir
        dnn_env['IMAGENET_PATH'] = os.path.join(self.data_root, "dnn/imagenet")
        dnn_env['TBENCH_QPS'] = str(params["qps"])
        dnn_env['TBENCH_MAXREQS'] = "1000000000" # Large enough to not terminate early
        dnn_env['TBENCH_WARMUPREQS'] = "100"
        dnn_env['TBENCH_MINSLEEPNS'] = "1000"
        dnn_env['OMP_NUM_THREADS'] = "4" # Limit to 4 threads
        dnn_cmd = [self.server_bin, '-r', '10000000',
            '-m', os.path.join(self.scratch_dir, 'custom_model.pt')]
        self.logger.info(subprocess.list2cmdline(dnn_cmd))

        dnn = subprocess.Popen(dnn_cmd, env=dnn_env, shell=False)
        with open(os.path.join(self.results_dir, "server.pid"), "w") as s:
            s.write("{}".format(dnn.pid))

        while not os.path.exists(server_tidfile):
            # Wait for worker thread to start up
            pass

        # Important: profile AFTER server has started up its threads!
        # needed so that partthyme doesn't pin all of the child threads to the
        # same core as the profiled parent process.
        time.sleep(10)

        dnn_psutil = psutil.Process(dnn.pid)
        dnn_psutil.cpu_affinity([0, 1, 2, 3, 4, 5, 6, 7])

        self.profiled_tids = []
        with open(server_tidfile) as f:
            pid = int(f.readline().strip())
            self.profiled_tids.append(str(pid))
            # Discover all worker threads and profile them
            # FIXME: Does not work properly since partthyme sometimes does
            # not correctly detach from multiple threads
            #dnn_psutil_proc = psutil.Process(pid)
            #with dnn_psutil_proc.oneshot(): # Purely performance boost
            #    dnn_threads = dnn_psutil_proc.threads()
            #    for thr in dnn_threads:
            #        if str(thr.id) not in self.profiled_tids:
            #            self.profiled_tids.append(str(thr.id))

        self.logger.info("Profiling threads: {}".format(self.profiled_tids))

        # Start profiling
        self.rawdata_dir = os.path.join(self.results_dir, "rawdata")

        if not self.measure_bbox:
            received_sigint = profile_threads(self.uarch, self.profiled_tids,
                "{}".format(header), True, self.rawdata_dir, self.mrc_phases,
                mrc_profile_period=self.mrc_period)

        received_sigint = profile_threads(self.uarch, self.profiled_tids,
            "{}".format(header), False, self.rawdata_dir, self.phases,
            mrc_profile_period=self.mrc_period)

        # Teardown
        dnn.kill()
        dnn.wait()
        os.remove(os.path.join(os.path.join(self.results_dir, "server.pid")))
        os.remove(server_tidfile)

        if received_sigint:
            self.logger.info("Received SIGINT, exiting...")
            sys.exit(1)

class XapianWorkload(Workload):

    def run(self, params, header):
        server_tidfile = os.path.join(self.scratch_dir, "tbench_server_tid.txt")

        # Clean up the worker thread id file possibly left from previous run.
        if os.path.exists(server_tidfile):
            os.remove(server_tidfile)

        qps = params['qps']
        skew = params['skew']
        tll = 100
        tul = params['terms_ulimit'] * 100
        nd  = 600000
        avgdl = params['avg_doc_len'] * 100

        xapian_env = os.environ.copy()
        xapian_env['SCRATCH_DIR'] = self.scratch_dir
        xapian_env['TBENCH_QPS'] = str(qps)
        xapian_env['TBENCH_ZIPF_SKEW'] = str(skew)
        xapian_env['TBENCH_MAXREQS'] = "1000000000" # Large enough to not terminate early
        xapian_env['TBENCH_WARMUPREQS'] = "100"
        xapian_env['TBENCH_MINSLEEPNS'] = "1000"

        dbrootpath = os.path.join(self.data_root, "xapian/stackoverflow-dbs")
        dbpath = os.path.join(dbrootpath, "nd{}_avgdl{}".format(nd, avgdl))
        termspath = os.path.join(dbrootpath, 'terms')
        xapian_env['TBENCH_TERMS_FILE'] = os.path.join(termspath, "nd{}_avgdl{}/terms_ul{}.in".format(nd, avgdl, tul))

        # Need to link shared library
        xapian_sharedlibpath = os.path.join(self.apps_root, "xapian/xapian-core-1.2.13/install/lib")
        if "LD_LIBRARY_PATH" in xapian_env:
            xapian_env['LD_LIBRARY_PATH'] += os.pathsep + xapian_sharedlibpath
        else:
            xapian_env['LD_LIBRARY_PATH'] = xapian_sharedlibpath

        cmd = [self.server_bin, '-r', '100000000', '-n', '1',
               '-d', dbpath
        ]
        self.logger.info(subprocess.list2cmdline(cmd))
        xapian = subprocess.Popen(cmd, env=xapian_env)
        with open(os.path.join(self.results_dir, "server.pid"), "w") as s:
            s.write("{}".format(xapian.pid))

        while not os.path.exists(server_tidfile):
            # Wait for worker thread to start up
            pass

        time.sleep(1)
        self.profiled_tids = []
        with open(server_tidfile) as f:
            self.profiled_tids.append(f.readline().strip())

        # Start profiling
        self.rawdata_dir = os.path.join(self.results_dir, "rawdata")

        if not self.measure_bbox:
            received_sigint = profile_threads(self.uarch, self.profiled_tids,
                "{}".format(header), True, self.rawdata_dir, self.mrc_phases,
                mrc_profile_period=self.mrc_period)

        received_sigint = profile_threads(self.uarch, self.profiled_tids,
            "{}".format(header), False, self.rawdata_dir, self.phases,
            mrc_profile_period=self.mrc_period)

        # Teardown
        xapian.kill()
        xapian.wait()
        os.remove(os.path.join(os.path.join(self.results_dir, "server.pid")))
        os.remove(server_tidfile)

        if received_sigint:
            self.logger.info("Received SIGINT, exiting...")
            sys.exit(1)

class SiloWorkload(Workload):

    def run(self, params, header):
        server_tidfile = os.path.join(self.scratch_dir, "tbench_server_tid.txt")

        # Convert the input parameters into arguments to pass to silo integrated harness.
        # The Tailbench harness accepts arguments via environment variables instead of command-line.
        # Note that for silo, we need to calculate fq_stocklevel from other txn frequencies
        # since they add up to 100.
        qps            = params["qps"]
        scale_factor   = params["scale_factor"]
        fq_neworder    = params["fq_neworder"]
        fq_payment     = params["fq_payment"]
        fq_delivery    = params["fq_delivery"]
        fq_orderstatus = params["fq_orderstatus"]
        fq_stocklevel = 100 - fq_neworder - fq_payment - fq_delivery - fq_orderstatus
        assert(fq_stocklevel > 0)

        silo_env = os.environ.copy()
        silo_env['SCRATCH_DIR'] = self.scratch_dir
        silo_env['TBENCH_QPS'] = str(qps)
        silo_env['TBENCH_MAXREQS'] = "1000000000" # Large enough to not terminate early
        silo_env['WORKLOAD'] = "tpcc"

        silo_env['TBENCH_WARMUPREQS'] = "20000"
        silo_env['FQ_NEWORDER'] = str(fq_neworder)
        silo_env['FQ_PAYMENT'] = str(fq_payment)
        silo_env['FQ_DELIVERY'] = str(fq_delivery)
        silo_env['FQ_ORDERSTATUS'] = str(fq_orderstatus)
        silo_env['FQ_STOCKLEVEL'] = str(fq_stocklevel)

        # Clean up the worker thread id file possibly left from previous run.
        if os.path.exists(server_tidfile):
            os.remove(server_tidfile)

        silo_cmd = ['numactl', '-C', '3,4,5,6,7,11,12,13,14,15',
            self.server_bin, '--bench', 'tpcc',
            '--num-threads', str(self.nthreads),
            '--scale-factor', str(params["scale_factor"]),
            '--retry-aborted-transactions',
            '--ops-per-worker', '1000000000000' # Large enough to not terminate early
            ]
        self.logger.info(silo_cmd)
        silo = subprocess.Popen(silo_cmd, env=silo_env)
        with open(os.path.join(self.results_dir, "server.pid"), "w") as s:
            s.write("{}".format(silo.pid))

        while not os.path.exists(server_tidfile):
            # Wait for worker thread to start up
            pass

        time.sleep(1)
        self.profiled_tids = []
        with open(server_tidfile) as f:
            self.profiled_tids.append(f.readline().strip())

        # Start profiling
        self.rawdata_dir = os.path.join(self.results_dir, "rawdata")

        if not self.measure_bbox:
            received_sigint = profile_threads(self.uarch, self.profiled_tids,
                "{}".format(header), True, self.rawdata_dir, self.mrc_phases,
                mrc_profile_period=self.mrc_period)

        received_sigint = profile_threads(self.uarch, self.profiled_tids,
            "{}".format(header), False, self.rawdata_dir, self.phases,
            mrc_profile_period=self.mrc_period)

        # Teardown
        silo.kill()
        silo.wait()
        os.remove(os.path.join(os.path.join(self.results_dir, "server.pid")))
        os.remove(server_tidfile)

        if received_sigint:
            self.logger.info("Received SIGINT, exiting...")
            sys.exit(1)

class MemcachedWorkload(Workload):

    # Construct a list with which to pass to command-line options
    # to the server and/or client
    # Each parameter is a type of a mutilate distribution object from
    # mutilate_distribs. If it is not technically a distribution (i.e., a
    # fixed number), the parameter is a Fixed type or a String.
    def construct_paramstr(self, params):
        parameters_string = ""

        for k,v in params.items():
            parameters_string += " {} {}".format(k, v.argstr())

        return parameters_string

    def run(self, params, header):

        server_tidfile = os.path.join(self.scratch_dir, "memcached_worker.tid")

        # Convert the input parameters to arguments to pass to the mutilate (load-generator) client
        # Use fixed point since mutilate has a limited size buffer for string inputs
        # Convert each params[i] to float in cases where the optimizer inserts
        # integer params
        params["key_mean"]     = float(Decimal(float(params["key_mean"])).quantize(Decimal('1.00')))
        params["key_stdev"]    = float(Decimal(float(params["key_stdev"])).quantize(Decimal('1.00')))
        params["val_mean"]     = float(Decimal(float(params["val_mean"])).quantize(Decimal('1.00')))
        params["val_stdev"]    = float(Decimal(float(params["val_stdev"])).quantize(Decimal('1.00')))
        params["update_ratio"] = float(Decimal(float(params["update_ratio"])).quantize(Decimal('1.00')))

        client_args = {"-K": Normal(params["key_mean"], params["key_stdev"]),
                    "-V": Normal(params["val_mean"], params["val_stdev"]),
                    "-q": Fixed(params["qps"], isint=True),
                    "-u": Fixed(params["update_ratio"])
            }

        # Scale QPS parameter by number of memcached worker threads
        client_args["-q"].value = client_args["-q"].value * self.nthreads

        # Construct the actual argument string
        paramstr = self.construct_paramstr(client_args)

        # Start two processes: memcached (the target to profile)
        # and mutilate (the load generator). Use numactl to pin mutilate to
        # cores to a different socket (if possible).
        # Note that we run mutilate twice, first for filling the records
        # and second for the actual run
        # We fix to 1M records for memcached.
        num_records = 1000000

        # Clean up the worker thread id file possibly left from previous run.
        if os.path.exists(server_tidfile):
            os.remove(server_tidfile)

        # Bind to NUMA node 0
        memcached_env = os.environ.copy()
        memcached_env['SCRATCH_DIR'] = self.scratch_dir
        memcached_cmd = ['numactl', '--cpunodebind=0', '--membind=0',
                self.server_bin, '-t', str(self.nthreads),
                '-u', self.user,
                '-m', '2048',
                '-I', '128m'
            ]
        self.logger.info(memcached_cmd)
        memcached = subprocess.Popen(memcached_cmd, env=memcached_env)
        time.sleep(2)
        memcached_pid = memcached.pid
        with open(os.path.join(self.results_dir, "server.pid"), "w") as s:
            s.write("{}".format(memcached_pid))

        while not os.path.exists(server_tidfile):
            # Wait for worker thread to start up
            pass

        time.sleep(1)
        self.profiled_tids = []
        with open(server_tidfile) as f:
            self.profiled_tids.append(f.readline().strip())

        # First run is just to load the records
        mutilate_path = self.client_bin
        self.logger.info("Mutilate paramstr: {}".format(paramstr))
        firstrun_optstr = "-s 127.0.0.1 -r {} --loadonly{}".format(num_records, paramstr)
        firstrun_exe = [mutilate_path] + firstrun_optstr.split(" ")
        firstrun = subprocess.Popen(firstrun_exe)
        firstrun.wait()

        print("MUTILATE PATH = {}".format(mutilate_path))

        time.sleep(1)

        # Second run actually starts generating the load
        secondrun_optstr = ""
        if len(self.numa_nodes) > 1:
            secondrun_optstr += "--cpunodebind=1 --membind=1 "
        else:
            # Fix to blb5 parameters for now
            secondrun_optstr += "-C 3,4,5,6,7,11,12,13,14,15 "
        secondrun_optstr += mutilate_path
        # 10 threads per memcached thread, but number of mutilate threads should
        # not exceed number of SMT threads per socket.
        # Assumptions: every node has same number of procs/threads
        mutilate_nthreads = self.nthreads * 10
        if mutilate_nthreads > len(self.node0_procs) * 2:
            mutilate_nthreads = 22

        secondrun_optstr += " --noload -s 127.0.0.1 -r {} -t 6000 -T {}".format(
            num_records, mutilate_nthreads)
        secondrun_optstr += " -c 10{}".format(paramstr)
        secondrun_exe = ["numactl"] + secondrun_optstr.split(" ")
        self.logger.info(secondrun_exe)
        mutilate = subprocess.Popen(secondrun_exe)
        with open(os.path.join(self.results_dir, "client.pid"), "w") as c:
            c.write("{}".format(mutilate.pid))

        # Start profiling
        time.sleep(5)
        self.rawdata_dir = os.path.join(self.results_dir, "rawdata")

        # Sometimes we only want to profile program counters, which is faster...
        if not self.measure_bbox:
            received_sigint = profile_threads(self.uarch, self.profiled_tids,
                "{}".format(header), True, self.rawdata_dir, self.mrc_phases,
                mrc_profile_period=self.mrc_period)

        received_sigint = profile_threads(self.uarch, self.profiled_tids,
            "{}".format(header), False, self.rawdata_dir, self.phases,
            mrc_profile_period=self.mrc_period)

        # Teardown
        mutilate.kill()
        mutilate.wait()
        os.remove(os.path.join(os.path.join(self.results_dir, "client.pid")))

        memcached.kill()
        memcached.wait()
        os.remove(os.path.join(os.path.join(self.results_dir, "server.pid")))
        time.sleep(1)

        if received_sigint:
            self.logger.info("Received SIGINT, exiting...")
            sys.exit(1)

