#!/usr/bin/env python3
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

import os, sys, subprocess
import pandas as pd
import scipy
from scipy import stats
import time
import random
import math
import numpy as np
import matplotlib.pyplot as plt
import argparse
import pickle
import copy
import yaml
from decimal import Decimal

import logging
import logging.config

from sklearn.metrics import mean_squared_error
from sklearn import preprocessing

from skopt import gp_minimize
from skopt import dump, load
from skopt.space import Real, Integer
from skopt.utils import use_named_args

from ax.service.ax_client import AxClient
from ax.modelbridge import get_sobol
from ax import *
from ax.modelbridge.generation_strategy import GenerationStrategy, GenerationStep
from ax.modelbridge.registry import Models, ModelRegistryBase
from ax.modelbridge.dispatch_utils import choose_generation_strategy
from ax.modelbridge.modelbridge_utils import get_pending_observation_features
from ax.utils.common.logger import get_logger

# Unfortunately, Python3 does not allow import * at function level
optcfg = None
with open("optimizer_configs.yml", "r") as stream:
    try:
        optcfg = yaml.safe_load(stream)
    except yaml.YAMLError as exc:
        print(exc)
        sys.exit(1)

# Import app-specific workload implementations
sys.path.append(optcfg['global']['apps_root'])
from workloads_dict import workloads_dict
from workloads import *
from params import all_params

## ARGUMENTS

parser = argparse.ArgumentParser(description="Generate parameters for synthetic benchmark dataset from profile data")

## REQUIRED positional args
parser.add_argument('wltype', type=str, metavar="WORKLOAD_TYPE",
    help="Workload we are generating parameterized benchmark for.")
parser.add_argument("target_profile_dir", metavar="TARGET_PROFILE_DIR", type=str,
    help="Absolute path to directory that contains the target workload profiles measured with partthyme.")
parser.add_argument("results_dir", metavar="RESULTS_DIR", type=str,
    help="Absolute path to results directory.")

## Optional args
# Overall choices, such as optimization method and seed
parser.add_argument('--method', type=str, default='new-bayesian',
    choices=['new-bayesian'],
    help="Optimization method for constructing synthetic benchmark parameters.")
parser.add_argument('--seed', type=str, default='fbprojectforryanlee',
    help="Seed for starting condition of parameter space search")
parser.add_argument('--ax_seed', type=int, default=1000,
    help="Seed for choosing initial sample points for the new-bayesian optimizer")
parser.add_argument("-u", "--uarch", type=str, default="broadwell",
    choices=['broadwell', 'skylake', 'skylake-old'],
    help="uArch of the machine the profiler is running on (Options: skylake, skylake-old, broadwell). Default is broadwell.")
parser.add_argument("-t", "--threads", type=int, default=1,
    help="Number of worker threads, currently only works for memcached.")


parser.add_argument("-o", "--evalopt",  action='store_true', default=False,
    help="Evaluate the optimum found from the run at --results_dir")
parser.add_argument("-m", "--maxiter",  type=int, default=200,
    help="Run the search process for this many iterations")
parser.add_argument("-s", "--skip-opt-eval",  action='store_true', default=False,
    help="Skip evaluating the optimum for many more phases after running datamime to completion.")

# Per-iteration specific options
parser.add_argument("-i", "--evaliter",  type=int, default=-1,
    help="Evaluate the parameters explored at iteration evaliter")
parser.add_argument("-p", "--phases",  type=int, default=1000,
    help="Evaluate optimum or specific iteration with specified number of phases")
parser.add_argument("--mrc-phases",  type=int, default=5500,
    help="Evaluate optimum or specific iteration with specified number of phases for the mrc profiling step")
parser.add_argument("--mrc-period",  type=int, default=10000,
    help="Evaluate optimum or specific iteration with specified number of mrc phase period (in Bcycles)")
parser.add_argument("--ipc_target",  type=float, default=-1,
    help="Target a specific average ipc. overrides normal optimizer routine if set to value greater than 0")
parser.add_argument("--mpki_target",  type=float, default=-1,
    help="Target a specific average mpki. overrides normal optimizer routine if set to value greater than 0")
parser.add_argument("--cost-threshold",  type=float, default=-1,
    help="End optimizer run if cost is below this value for 3 consecutive iterations")
parser.add_argument("--saved_ax_client", type=str, default=None,
    help="Resume optimization from specified Ax Client JSON file")

# [FIXME] Ugh, have to make these globals since an objective function does not allow passing
# additional args other than the parameters
wl = None
logger = None

def bayesopt_objective_fn(_params):
    global wl, logger

    # Except for new-bayesian, the objective function accepts a list of parameters
    # instead of a dict.
    # In this case, convert to a dict using all_params
    # Note that the order of parameter values in _params must match the order in all_params
    if not isinstance(_params, dict):
        params = {}
        for i, param_details in enumerate(all_params[wl.wltype]["parameters"]):
            params[param_details["name"]] = _params[i]
    else:
        params = _params

    # Log parameters unless we are only evaluating a single point
    pnames = ""
    pvals = ""
    for k, v in params.items():
        pnames = pnames + "{},".format(k)
        pvals = pvals + "{},".format(v)
    wl.logger.info("Params:{}".format(pnames))
    wl.logger.info(pvals)

    if wl.runs == 0:
        wl.params_out.write(pnames[:len(pnames)-1] + "\n")
    wl.params_out.write(pvals[:len(pvals)-1] + "\n")
    wl.params_out.flush()
    wl.params[wl.runs] = params

    # Run the publicly available application with a synthetic dataset that is
    # characterized by the input parameters
    wl.run(params, header=str(wl.runs))
    wl.runs = wl.runs + 1

    # Calculate the cost of the most recent run
    ctrs, mrcs, ipcs = wl.read_profile(wl.runs-1)
    cost = wl.cost(ctrs, mrcs, ipcs)

    # Returning the cost will allow the optimizer to choose the next point in the
    # parameter space to evaluate.
    return {"Cost": cost}

def optimize(method, objective_fn, bounds, start, maxiter, ax_seed,
			 params = None, constraints = None, results_dir = None,
			 saved_ax_client=None):
    global logger
    ret = None
    if method == 'new-bayesian':
        if results_dir == None:
            sys.exit("Ax bayesian requires results path argument")

        if saved_ax_client != None:
            ax_client = AxClient.load_from_json_file(filepath=saved_ax_client)
        else:
            n_sobol_runs = min(15, len(params) * 3)
            gs = GenerationStrategy(
                steps=[
                    # 1. Initialization step (does not require pre-existing data
                    # and is well-suited for initial sampling of the search space)
                    GenerationStep(
                        model=Models.SOBOL,
                        num_trials=n_sobol_runs,  # How many trials should be produced from this generation step
                        min_trials_observed=n_sobol_runs, # How many trials need to be completed to move to next model
                        #min_trials_observed=len(params) * 4, # How many trials need to be completed to move to next model
                        max_parallelism=1,  # Max parallelism for this step
                        model_kwargs={"seed": ax_seed},  # Any kwargs you want passed into the model
                        model_gen_kwargs={},  # Any kwargs you want passed to `modelbridge.gen`
                    ),
                    # 2. Bayesian optimization step (requires data obtained from previous phase and learns
                    # from all data available at the time of each new candidate generation call)
                    GenerationStep(
                        model=Models.BOTORCH,
                        num_trials=-1,  # No limitation on how many trials should be produced from this step
                        max_parallelism=1,  # Parallelism limit for this step, often lower than for Sobol
                        # More on parallelism vs. required samples in BayesOpt:
                        # https://ax.dev/docs/bayesopt.html#tradeoff-between-parallelism-and-total-number-of-trials
                    ),
                ]
            )
            ax_client = AxClient(
		    	generation_strategy=gs,
                verbose_logging=True
		    )
            ax_client.create_experiment(
                name="BayesOpt",
                parameters = params,
                objective_name = "Cost",
                minimize = True,
                parameter_constraints = constraints
            )

        for i in range(maxiter):
            logger.info(f"Running GP+EI optimization trial {i + 1}/{maxiter}...")
            # Reinitialize GP+EI model at each step with updated data.
            parameters, trial_index = ax_client.get_next_trial()
            ax_client.complete_trial(trial_index=trial_index,
                raw_data=objective_fn(parameters))

        best_parameters, values = ax_client.get_best_parameters()
        logger.info("Best parameters: {}".format(best_parameters))

        ax_client.save_to_json_file(filepath=os.path.join(results_dir, "ax_client.json"))

        return (best_parameters, None)

    else:
        sys.exit("Invalid optimization method: {}".format(args.method))

def gen_bounds(wltype):
    lb = []
    ub = []
    for p in all_params[wltype]["parameters"]:
        lb.append(p["bounds"][0])
        ub.append(p["bounds"][1])

    return lb, ub

def main(argv):
    args = parser.parse_args()

    results_dir = args.results_dir
    target_profile_dir = args.target_profile_dir

    if not os.path.exists(results_dir):
        os.makedirs(results_dir)
    elif not args.evalopt and args.evaliter < 0:
        if len(os.listdir(results_dir)) != 0:
            print("[DATAMIME] Error: Results directory is not empty")
            sys.exit("Terminating.")

    rawdata_dir = os.path.join(results_dir, "rawdata")
    if not os.path.exists(rawdata_dir):
        os.makedirs(rawdata_dir)

    # Configure logging
    global logger
    logger = logging.getLogger(__name__)
    logger.setLevel(logging.DEBUG)

    # create console handler and set level to info
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    formatter = logging.Formatter('[%(levelname)s] %(message)s')
    ch.setFormatter(formatter)
    logger.addHandler(ch)

    # create file handler and set level to info
    fh = logging.FileHandler(os.path.join(results_dir, "datamime.log"))
    fh.setLevel(logging.INFO)
    formatter = logging.Formatter('[%(levelname)s - %(name)s -  %(asctime)s ] %(message)s')
    fh.setFormatter(formatter)
    logger.addHandler(fh)

    logging.getLogger('ax').addHandler(ch)
    logging.getLogger('ax').addHandler(fh)

    assert(args.threads >= 1)
    random.seed(args.seed)
    logger.info("Workload: {}".format(args.wltype))

    # Load optimizer and app configs
    incfg = None
    with open("optimizer_configs.yml", "r") as stream:
        try:
            incfg = yaml.safe_load(stream)
        except yaml.YAMLError as exc:
            print(exc)
            sys.exit(1)

    apps_incfg = None
    apps_root = incfg['global']['apps_root']
    with open(os.path.join(apps_root, "app_configs.yml"), "r") as stream:
        try:
            apps_incfg = yaml.safe_load(stream)
        except yaml.YAMLError as exc:
            print(exc)
            sys.exit(1)

    # Merge configs
    incfg = {**incfg, **apps_incfg}

    onlyeval = args.evalopt or args.evaliter >= 0

    global wl, workloads_dict
    try:
        wl = workloads_dict[args.wltype](args.uarch, args.wltype, args.threads,
            target_profile_dir, results_dir, incfg, logger, onlyeval=onlyeval,
            phases=args.phases, mrc_period=args.mrc_period,
            mrc_phases=args.mrc_phases)
    except KeyError:
        logger.info("Unidentified workload type {}".format(args.wltype))

    # Evaluate the optimum or a specific iteration instead of searching the
    # dataset space, then exit.
    if args.evalopt:
        # Read cfg file
        outcfg = None
        with open(os.path.join(results_dir, "outcfg.yml"), "r") as stream:
            try:
                outcfg = yaml.safe_load(stream)
            except yaml.YAMLError as exc:
                print(exc)
                sys.exit(1)

        optparams = outcfg["results"]["optimum_params"]
        logger.info("Evaluationg parameters {}".format(optparams))
        wl.phases = 10000
        wl.mrc_phases = 55000
        wl.run(optparams, header="opt/opt")
        sys.exit(0)
    elif args.evaliter >= 0:
        _, _, labels, loaded_params = load_costs_params(results_dir)
        params_to_eval = loaded_params[:, args.evaliter]
        logger.info("Evaluating iteration {}".format(args.evaliter))
        logger.info(params_to_eval)
        wl.run(construct_params(labels, params_to_eval),
                      header="iter{}/iter{}_".format(args.evaliter, args.evaliter))
        sys.exit(0)

    ###########################################################################
    # Search the dataset space
    ###########################################################################

    objective_fn = bayesopt_objective_fn
    parameters = all_params[args.wltype]["parameters"]
    constraints = all_params[args.wltype]["constraints"]
    outcfg = copy.deepcopy(incfg)

    # Read bounds from all_params
    lb, ub = gen_bounds(wl.wltype)
    start = [l + (u - l) * random.uniform(0,1) for l,u in zip(lb, ub)]

    with open(os.path.join(results_dir,"outcfg.yml"), 'w') as outcfg_file:
        usercfg = {
            'method':args.method,
            'seed':args.seed,
            'ax_seed': args.ax_seed,
            'threads': args.threads,
            'target_profile_dir':target_profile_dir,
            'results_dir':results_dir,
            'lower_bounds':lb,
            'upper_bounds':ub,
            'params': [ p["name"] for p in parameters ]
        }
        outcfg['user-specified configs'] = usercfg
        outcfg['all_params'] = all_params
        yaml.dump(outcfg, outcfg_file, default_flow_style=False, sort_keys=False)

    # Wrapper function that invokes the actual optimization routine based on the
    # user's chosen optimization method
    # (TODO) hrlee: currently only supports new-bayesian (i.e., Bayesian optimization
    # using the Ax framework).
    params_at_opt, model = optimize(args.method, objective_fn, list(zip(lb,ub)),
                   start, args.maxiter, args.ax_seed,
                   params=parameters, constraints=constraints, results_dir=results_dir,
                   saved_ax_client=args.saved_ax_client)

    # output rest of config information
    outcfg_to_append_to = {}
    with open(os.path.join(results_dir,"outcfg.yml"), 'r') as outcfg_file:
        try:
            outcfg_to_append_to = yaml.safe_load(outcfg_file)
        except yaml.YAMLError as exc:
            print(exc)
            sys.exit(1)

        results = {
            'results': {
                'optimum_params':params_at_opt
            }
        }
        outcfg_to_append_to.update(results)

    with open(os.path.join(results_dir,"outcfg.yml"), 'w') as outcfg_file:
        yaml.safe_dump(outcfg_to_append_to, outcfg_file, default_flow_style=False, sort_keys=False)

    if not args.skip_opt_eval:
        # Evaluate optimum with more samples
        wl.phases = 10000
        wl.mrc_phases = 55000
        logger.info("Evaluationg parameters {}".format(params_at_opt))
        wl.run(params_at_opt, header="opt/opt")

if __name__ == "__main__":
    main(sys.argv)
