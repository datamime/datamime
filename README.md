# Datamime: Benchmark Generation using Automatic Dataset Synthesis

Datamime is a tool that generates representative benchmarks that closely mimic
the behavior of target workloads. Datamime profiles the target workload and
generates a synthetic *dataset* that, when run by a benchmark program (which
can be the same or similar to the target workload's), closely mimics the
behavior of the target workload. Please see the
[Datamime paper](https://people.csail.mit.edu/sanchez/2022.datamime.micro.pdf)
for more details.

Datamime consists of a profiling tool (`profiler/datamime-profiler`) 
and a dataset search program (`search/search\_dataset.py`) that work in tandem to 
produce a representative dataset. The overall usage flow of Datamime is as follows:

1. Define the hardware counters that will be used to measure hardware performance metrics
of interest. The performance counters to measure are defined in `profiler/events.py`. 

We recommend using the same machine to profile the target workload and to
generate the benchmark, as differences in the hardware/software stack can make
it difficult to generate representative synthetic datasets.

2. Profile the target workload with `datamime-profiler`. This will produce a set of profiles
that include hardware performance counters, LLC miss-curves, and ipc-curves measured using
Intel CAT and CMT.

3. Invoke the dataset search process with `search_dataset.py`. Step-by-step
guidelines are given below.

`apps/` contains the applications Datamime uses to create benchmarks.
Please consult `apps/README` for instructions on how to build the apps,
pre-generate necessary data, and add new applications.

The following sections describe how to use `datamime-profiler` and the steps needed to set up
dataset generation.

## Dependencies
* Tested on Ubuntu 18.04
* Python 3.7 or higher, g++-7.5
* Install [armadillo: C++ Linear Algebra Library](http://arma.sourceforge.net/)
* Make sure [Intel's Cache Allocation Technology (CAT) and Cache Monitoring Technology (CMT)](https://www.intel.com/content/www/us/en/communications/cache-monitoring-cache-allocation-technologies.html)
are supported. 
* Linux packages: msr-tools, libnuma-dev, scons, libevent-dev, numactl, acpu-support, acpid, acpi
* Python package: [ax](https://ax.dev/docs/installation.html), numpy, scipy, sklearn, skopt, scikit-optimize, yaml, prettytable, pandas, matplotlib, psutil

## Datamime Profiler

### Overview

`datamime-profiler` is a tool that records hardware performance counters 
of multithreaded applications over time, and gather miss and IPC curves
using LLC partitioning support on Intel multicore processors. 

### Build and Setup

1. From `datamime/profiler`, run `make init`. This will download `libpfm` into the `lib` directory and build it.
2. Edit the Makefile so that `ARMADILLO_PATH` points to your armadillo installation.
3. Run `make` to build `datamime-profiler`.
4. Lastly, run `modprobe msr` to explicitly load the msr driver.
Also make sure that you turn off Intel Turboboost and edit perf_event_paranoid
so that perf can measure processes. We provide a convenience script 
(`setup_pstate_msr`) that sets up all of these steps automatically that you can
find in the root directory of the repo.

After building the profiler, you can build and profile a microbenchmark that 
traverses a 6MB array to verify that it is working.
You can pass anything for the tscfreq argument for now; we explain how to obtain
the tsc frequency of your system below.

```
profiler$ cd ../microbenchmarks
microbenchmarks$ make && cd ../profiler
profiler$ ../microbenchmarks/traverse-array -s 6000 -i 1000000000 &
profiler$ ./harness.py outfile_header tscfreq tid_of_microbenchmark
```

### Running the Datamime profiler

We provide a Python harness around `datamime-profiler` for ease of use:

```
Usage: ./harness.py [--mrc] [-p NUM_PHASES] [-d RESULTS_DIR] outfile_header tscfreq tid1 tid2 ...
```

**Note that you need to run `profiler` with root privileges, since it reads/writes
`/dev/cpu/CPUNUM/msr`, which is protected. We explain how to run `profiler` in
user mode below.**

`outfile_header` positional argument is used to prepend the results files of the profiler.
`tscfreq` should be set to the TSC frequency of your machine such that memory bandwidth
(in terms of B/s) can be measured from the profiled data. We provide a tool to
measure TSC frequency using CPUID (`tools/tsc-freq`), and you can also measure it
by using a [publicly available Linux kernel driver](https://github.com/trailofbits/tsc_freq_khz).

The `--mrc` option is used to toggle MRC and IPC curve collection by `datamime-profiler`.
Note that `datamime-profiler` currently does not support collecting performance counters
for user-specified events when this option is provided (it only collects a set of events necessary for
miss curve and IPC curve estimation). 

You can also change the number of phases `datamime-profiler` will profile the target
thread with the `-p` option. Default is 5500 phases.

The results are stored by default on `$profiler/output/`. You can use the 
`-d RESULTS_DIR` option to specify the directory where you want to store the
output profiles dumped by the profiler.

### Output

`datamime-profiler` will generate raw output files with the `outfile_header` you
supply to the harness. There will be multiple files per thread profiled,
depending on the `--mrc` option:
* `<outfile_header>_counters_<tid>` contains basic raw performance counter values
generated with the `--mrc` option
* `<outfile_header>_grouped_counters_<tid>` contains grouped raw performance counter values
as specified in `harness.py` when run without the `--mrc` option
* `*.csv` represents the (either basic or grouped) performance counters
in a CSV format.
* `<outfile_header>_mrc_<tid>` contains a table of miss curves sampled at a 10B cycle
interval. Each column represents a sample a given time interval, and each
row is the number of ways allocated in increasing order (e.g., first row
is a single-way allocation). Each entry in the table is the profiled MPKI.
* `<outfile_header>_ipc_<tid>` contains a table of ipc curves sampled at a 10B
cycle interval. The rows and columns are the same as the miss curves output,
and each entry is the profiled IPC.
* a YAML file called `target_configs.yml` will be created with the TSC frequency
listed in it. This is used to calculate the memory bandwidth metric.

### Running datamime-profiler in user mode

Internally, `datamime-profiler` modifies the MSR registers to read program counters and
set configurations for Intel CAT and CMT. Changes to the Linux kernel since 3.7
requires executables to have capability CAP\_SYS\_RAWIO to open the MSR device file.
The simplest workaround is to run `datamime-profiler` (and any other programs that call
it) with root permissions, but sometimes this is not desirable for many reasons.

To enable executing `datamime-profiler` from userspace, follow the steps below
(taken from [here](https://stackoverflow.com/questions/18661976/reading-dev-cpu-msr-from-userspace-operation-not-permitted).

* Set `datamime-profiler` to have CAP\_SYS\_RAWIO capability: 
```
sudo setcap cap_sys_rawio=ep datamime-profiler
```
* Add a custom group which will have access to the msr (need to relogin to have effect):
```
groupadd msr
chgrp msr /dev/cpu/*/msr
chmod g+rw /dev/cpu/*/msr
```
* Assign group to the user:
```
usermod -aG msr myuser
```

* Lastly, allow seizing processes:
```
echo 0 > /proc/sys/kernel/yama/ptrace_scope
```

## Datamime Usage

`search_dataset.py` forms the dataset generation as an optimization problem
where it instantiates the target open-source workload with the current set of dataset
parameters it is evaluating, calculate the difference in profiles, and use
the resulting difference as the cost to minimize.

Note: Since Datamime uses `datamime-profiler` to profile the candidate benchmark it generates,
you need to either run it with root permissions or follow the steps above to
allow user-mode execution.

Follow these steps to run Datamime:

1. Make sure that that you set up and build the profiler (`datamime-profiler`)
since it will be used by Datamime to profile each candidate benchmark.
2. Build the application you want to use to generate your benchmark within the 
Datamime apps directory. The four apps we provide will have its own build script you can use. 
Please consult the apps README for details on this step (the README will also 
guide you on how you can add a new application to generate benchmarks).
3. Edit `optimizer_configs.yml`. You will need to provide the processor TSC frequency
(needed for measuring memory bandwidth), paths to the scratch/data/apps directories,
path to your Python3 installation. You can also edit metrics you want to measure
and their weights.
4. Define how the performance counters will be used to generate the metrics of interest
that you specified in Step 3. This can be done by editing the `generate_stats()` function, 
located within `cost_model.py`
(e.g., several different counters may need to be added up to produce a single metric).

Now, you should be all set to run the dataset generation via the following:

```
Usage: ./search_dataset.py appname target_profile_path results_path [options]
```

`appname` is the name of the application Datamime uses to build its benchmark,
and it should refer to one of the keys of `workload_dict`
(defined within `apps/workloads_dict.py`). Note again that we provide 4 applications
that already have dataset generators implemented (memcached, xapian, silo, dnn)
and you can add your own.

`target_profile_path` should be the path to the directory where you stored the profiles
of the target workload obtained using the Datamime profiler, and `results_path`
should be where you want to store all of Datamime's output.

Please do not run any other programs while Datamime is running to minimize noise.
After the Datamime finishes, you can profile the resulting benchmark with the optimal
set of dataset parameters for longer by passing the -o option and rerunning
the above command with a higher number of evaluation phases
(Datamime will automatically do this unless you pass the -s option).

### Debugging via logs

Datamime will produce a log in `RESULTS_PATH/datamime.log`.
Additionally, the two `datamime-profiler` runs of each optimizer iteration will be 
logged in `RESULTS_PATH/rawdata/<iteration number>_datamime-profiler.log`. Please
consult these logs to debug your optimizer runs should they encounter any
errors.

## Limitations

Datamime has been tested to match the behavior of a single worker thread. 
Matching the behavior of multiple worker threads is a work in progress.

`datamime-profiler` currently only supports profiling a limited amount of threads.
The maximum amount of threads it can profile is limited by the number of
hardware threads in a single CPU (i.e., a single socket).

For multi-socket systems, `datamime-profiler` will restrict all threads to the first
NUMA node, assuming that there is a 1-to-1 mapping of NUMA nodes to sockets.
