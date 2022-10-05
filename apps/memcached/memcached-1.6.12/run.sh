#!/bin/bash
# Simple script for running memcached with correct configs

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source ${DIR}/../../run_configs.sh

rm ${SCRATCH_DIR}/memcached_worker.tid

SCRATCH_DIR=${SCRATCH_DIR} \
./memcached -t 1 -l 127.0.0.1 -m 2048 -I 128m
