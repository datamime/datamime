#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source ${DIR}/../run_configs.sh

# Path for shared library for xapian
# From: https://stackoverflow.com/questions/62873982/linux-check-if-path-exists
appendpath () {
    case ":$LD_LIBRARY_PATH:" in
        *:"$1":*)
            ;;
        *)
            LD_LIBRARY_PATH="${LD_LIBRARY_PATHPATH:+$LD_LIBRARY_PATH:}$1"
    esac
}

appendpath "${PWD}/xapian-core-1.2.13/install/lib"


NSERVERS=1

# Parameters from datamime
SO_QPS=622
SO_SKEW=2.3998277408885427
SO_AVGDOCLEN=400
SO_TERMUL=4700

# Parameters from camera-ready datamime
CR_SO_QPS=385
CR_SO_SKEW=0.0
CR_SO_AVGDOCLEN=400
CR_SO_TERMUL=1800

WIKI_QPS=400
WIKI_SKEW=1.1
WARMUPREQS=1000
REQUESTS=3000000

DIFFTARGET_QPS=100
DIFFTARGET_SKEW=0.0
DIFFTARGET_TERMUL=200

# Camera-ready
CR_SO_ROOT=${DATA_ROOT}/xapian/stackoverflow-dbs
CR_SO_DATA=nd600000_avgdl${CR_SO_AVGDOCLEN}

if [ "$1" == "target" ]; then
    echo "Starting target"
    TBENCH_MAXREQS=${REQUESTS} TBENCH_WARMUPREQS=${WARMUPREQS} \
        TBENCH_QPS=${WIKI_QPS} TBENCH_MINSLEEPNS=100000 \
        TBENCH_TERMS_FILE=${DATA_ROOT}/xapian/wiki/terms-with-freq-msetub1000.in \
        TBENCH_ZIPF_SKEW=${WIKI_SKEW} \
        SCRATCH_DIR=${SCRATCH_DIR} \
       ./xapian_integrated -n ${NSERVERS} -d ${DATA_ROOT}/xapian/wiki/wiki -r 100000000000
elif [ "$1" == "datamime-cameraready" ]; then
    echo "Starting camera-ready datamime"
    TBENCH_MAXREQS=${REQUESTS} TBENCH_WARMUPREQS=${WARMUPREQS} \
        TBENCH_QPS=${CR_SO_QPS} TBENCH_MINSLEEPNS=100000 \
        TBENCH_TERMS_FILE=${CR_SO_ROOT}/terms/${CR_SO_DATA}/terms_ul${CR_SO_TERMUL}.in \
        TBENCH_ZIPF_SKEW=${CR_SO_SKEW} \
        SCRATCH_DIR=${SCRATCH_DIR} \
       ./xapian_integrated -n ${NSERVERS} -d ${CR_SO_ROOT}/${CR_SO_DATA} -r 100000000000
else
    echo "Starting difftarget"
    TBENCH_MAXREQS=${REQUESTS} TBENCH_WARMUPREQS=${WARMUPREQS} \
        TBENCH_QPS=${DIFFTARGET_QPS} TBENCH_MINSLEEPNS=100000 \
        TBENCH_TERMS_FILE=${SO_ROOT}/terms/terms_ll100_ul${DIFFTARGET_TERMUL}.in \
        TBENCH_ZIPF_SKEW=${DIFFTARGET_SKEW} \
        SCRATCH_DIR=${SCRATCH_DIR} \
       ./xapian_integrated -n ${NSERVERS} -d ${SO_ROOT}/${SO_DATA} -r 100000000000
fi
