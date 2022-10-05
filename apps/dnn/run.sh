#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source ${DIR}/../run_configs.sh

WARMUPREQS=10
REQUESTS=3000000

# Point to the path of your imagenet collection
IMAGENET_PATH=${DATA_ROOT}/dnn/imagenet

if [ "$1" == "target" ]; then
    echo "Starting target"
    QPS=11
    NETWORK="${DATA_ROOT}/dnn/resnet50_model.pt"
elif [ "$1" == "datamime" ]; then
    echo "Starting datamime"
    QPS=5.97
    NETWORK="/path/to/model/produced/by/datamime.pt"
elif [ "$1" == "datamime-cameraready" ]; then
    echo "Starting camera-ready datamime"
    QPS=34.08
    NETWORK="/path/to/model/produced/by/datamime.pt"
else
    echo "Starting difftarget"
    QPS=10
    NETWORK="${DATA_ROOT}/dnn/shufflenet_v2_x1_0_model.pt"
fi

TBENCH_QPS=${QPS} TBENCH_MAXREQS=${REQUESTS} TBENCH_WARMUPREQS=${WARMUPREQS} \
       TBENCH_MINSLEEPNS=10000 OMP_NUM_THREADS=4 \
       IMAGENET_PATH=${IMAGENET_PATH} \
       SCRATCH_DIR=${SCRATCH_DIR} \
       ./build/dnn_integrated -m ${NETWORK} -r 100000000
