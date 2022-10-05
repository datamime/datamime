# Datamime Applications

This repository contains the following four applications used to evaluate Datamime:

- Memcached (`memcached`)
- Silo (`silo`)
- Xapian (`xapian`)
- Dnn-as-a-service (`dnn`)

In addition, the `harness` directory contains the Tailbench harness that is used
by the silo, xapian, and dnn.

## Dependencies

- Tested on Ubuntu 18.04
- Requirements for each application are listed under inidividual app READMEs.
- Note that Xapian require gcc/g++-4.8 to build.

## Initial Setup

First, build each application by invoking the provided build scripts `build.sh`.
silo, xapian, and dnn utilize the Tailbench harness, 
so harness must be built first before building these applications. 

Next, you should set up the `run_configs.sh`:
- Set `DATA_ROOT` to point to the directory generated by `scripts/generate_data.sh`.
This directory contains the pre-generated indexes and terms for Xapian,
and other traces needed to re-create the Target workloads in the original Datamime paper.
See the "Downloading traces and pre-generating datasets" section for details.
- Set `SCRATCH_DIR` to point to a temporary scratch space where the applications will
store their thread id file to communicate with Datamime.

At this point, you should be able to use the provided apps to generate benchmarks
with Datamime.

## Downloading traces and pre-generating datasets

The Datamime MICRO-55 results use target workloads that are driven from certain traces.
In addition, the current implementation of the dataset generator for xapian requires
the indexes and terms to be generated a priori to reduce search time.

Thus, you need to run the dataset generation script in `scripts/generate_data.sh`
by targeting a specific directory,
and point DATA_ROOT within `run_configs.sh` to the directory.

*Important*: For dnn, you need to download the 2012 Imagenet validation dataset _manually_ by requesting access to
the dataset (see [here](https://image-net.org/download.php)).
Once downloaded, modify the validation dataset to a format accepted by our dnn application 
by running `valprep.sh`, and make sure the whole imagenet dataset resides under
`<DATA_ROOT>/dnn/imagenet`.

If you cannot get access to the full Imagenet dataset,
a partial subset is available in [Kaggle](https://www.kaggle.com/competitions/imagenet-object-localization-challenge/data).

## Running applications individually

Each app directory contains a run script (`run.sh`) that sets up the parameters
and runs the application with datasets evaluated in the Datamime paper --
Target(target), Different Dataset (difftarget), and Datamime (datamime).
Use these to run the application with a custom set of parameters of your choice
(e.g., you may want to run the application with dataset parameters found by Datamime
without Datamime profiling it).

## Adding a new application

1. Define the parameters and bounds of your synthetic dataset within `params.py`.
Note that the Bayesian optimizer also admits logarithmic search spaces, which is
useful when searching dimensions with large orders of magnitude differences
in the bounds.

2. Define a new class within `workloads.py` that inherits the `Workloads` abstract base class.
You will need to implement the `run()` abstract method that defines how your application
is driven with a dataset generator that takes in the dataset parameters.
The dataset generator can be a separate binary you instantiate with the given parameters
(see memcached) or integrated into the application itself (see xapian and silo).

You should also add your new class in `workloads_dict` so that Datamime is able find it.

3. Add the path to your server and client binaries in `apps_configs.yml`.
Note that the two should be the same if your application is not request-driven
or if the server and client are integrated into the same binary (e.g., using
the Tailbench integrated harness).

## Miscellaneous

When re-creating the `mem-twtr` Target workload results, use `mutilate-twtr`
instead of `mutilate`. The former is a significantly modified version of mutilate
that can read and replay a Twemcache trace. Unfortunately the code changes are
significant enough that we currently cannot merge the two versions of mutilate.