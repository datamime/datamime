#!/bin/bash
# ops-per-worker is set to a very large value, so that TBENCH_MAXREQS controls how
# many ops are performed

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source ${DIR}/../run_configs.sh

NUM_THREADS=1
MAXREQS=1000000000
WARMUPREQS=20000

if [ "$1" == "target" ]; then
    echo "Starting target"
    QPS=150000
    NUM_WAREHOUSES=2000
    WL=bid
    # These don't matter for bidding benchmark
    DM_FQ_NEWORDER=1
    DM_FQ_PAYMENT=1
    DM_FQ_DELIVERY=1
    DM_FQ_ORDERSTATUS=1
    DM_FQ_STOCKLEVEL=1
elif [ "$1" == "datamime" ]; then
    echo "Starting datamime"
    QPS=100000
    NUM_WAREHOUSES=20
    WL=tpcc
    DM_FQ_NEWORDER=1
    DM_FQ_PAYMENT=1
    DM_FQ_DELIVERY=20
    DM_FQ_ORDERSTATUS=77
    DM_FQ_STOCKLEVEL=1
elif [ "$1" == "datamime-cameraready" ]; then
    echo "Starting camera-ready datamime"
    QPS=100000
    NUM_WAREHOUSES=13
    WL=tpcc
    DM_FQ_NEWORDER=1
    DM_FQ_PAYMENT=1
    DM_FQ_DELIVERY=1
    DM_FQ_ORDERSTATUS=96
    DM_FQ_STOCKLEVEL=1
else
    echo "Starting difftarget"
    QPS=100000
    NUM_WAREHOUSES=1
    WL=tpcc
    DM_FQ_NEWORDER=45
    DM_FQ_PAYMENT=43
    DM_FQ_DELIVERY=4
    DM_FQ_ORDERSTATUS=4
    DM_FQ_STOCKLEVEL=4
fi

SCRATCH_DIR=${SCRATCH_DIR} \
TBENCH_QPS=${QPS} TBENCH_MAXREQS=${MAXREQS} TBENCH_WARMUPREQS=${WARMUPREQS} \
FQ_NEWORDER=${DM_FQ_NEWORDER} \
FQ_PAYMENT=${DM_FQ_PAYMENT} \
FQ_DELIVERY=${DM_FQ_DELIVERY} \
FQ_ORDERSTATUS=${DM_FQ_ORDERSTATUS} \
FQ_STOCKLEVEL=${DM_FQ_STOCKLEVEL} \
    TBENCH_MINSLEEPNS=1000\
    WORKLOAD=${WL} \
    ./out-perf.masstree/benchmarks/dbtest_integrated --verbose \
    --bench ${WL} --num-threads ${NUM_THREADS} --scale-factor ${NUM_WAREHOUSES} \
    --retry-aborted-transactions --ops-per-worker 1000000000

echo $!
