#!/bin/bash


if [ "$1" == "target" ]; then
    echo "Starting target"
    QPS=5
    KEY=fb_key
    VAL=fb_value
    UR=0.1
elif [ "$1" == "datamime" ]; then
    echo "Starting datamime"
    QPS=55601
    KEY=normal:10,6.5
    VAL=normal:51.5,100
    UR=0.4
elif [ "$1" == "datamime-cameraready" ]; then
    echo "Starting camera-ready datamime"
    QPS=54830
    KEY=normal:10,8.107
    VAL=normal:118.75,100.0
    UR=0.456
elif [ "$1" == "datamime-twtr-cameraready" ]; then
    echo "Starting camera-ready datamime that matches mem-twtr"
    QPS=23597
    KEY=normal:66.53,6.7
    VAL=normal:10.0,100.0
    UR=0.622
else
    echo "Starting difftarget"
    QPS=1000
    KEY=23
    VAL=18
    UR=0.5
fi

taskset --cpu-list 3-7,11-15 ./mutilate -s 127.0.0.1 -q ${QPS} -K ${KEY} -V ${VAL} -u ${UR} -r 1000000 -T 10 -c 10 --loadonly
echo "Finished loading all records"
taskset --cpu-list 3-7,11-15 ./mutilate -s 127.0.0.1 -q ${QPS} -K ${KEY} -V ${VAL} -u ${UR} -r 1000000 -T 10 -c 10 --noload -t 10000000
