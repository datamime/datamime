#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source ${DIR}/../../run_configs.sh

echo "Starting twemcache cluster 27"
QPS=24550
KEY=1
VAL=1
UR=1

taskset --cpu-list 3-7,11-15 ./mutilate -s 127.0.0.1 -q ${QPS} --twemcache_path ${DATA_ROOT}/memcached/cluster27.0_firstbillion --twitter -r 1000000 -T 1 --noload -t 10000000 --nreqs 100000000
