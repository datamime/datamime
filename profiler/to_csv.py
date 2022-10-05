#!/usr/bin/python3
import sys
from collections import defaultdict as ddict
import numpy as np
import math
import os


def separate_headers(thyme_log):
    headers, data = [], []
    for line in thyme_log:
        if line[0] == "group":
            headers.append(line)
        else:
            data.append(line)
    return headers, data

def get_groups(headers):
    groups = ddict(dict)
    all_events = [ "UNHALTED_REFERENCE_CYCLES", "INST_RETIRED", "CPU_CLK_UNHALTED" ]
    for line in headers:
        group_id = line[1]
        events = line[2:]
        for i in range(len(events)):
            groups[group_id][events[i]] = i
        all_events.extend(events[3:])
    return groups, all_events

def to_csv(filename, rdt = True):

    thyme_log = [ l.strip().split() for l in open(filename, "r").readlines() ]
    headers, data = separate_headers(thyme_log)

    groups, events = get_groups(headers)
    non_group_ctrs = 7
    if rdt:
        columns = ["timestamp", "ref_cycles", "time_running", "time_enabled", "local_mem_traffic", "l3_occupancy"] + events
        non_group_ctrs = 9
    else:
        columns = ["timestamp", "ref_cycles", "time_running", "time_enabled"] + events
    column_idxs = { name : idx for idx, name in enumerate(columns) }

    total_tsc = 0
    totals = ddict(dict)

    rows = []

    last_local_mem_traffic = 0
    last_l3_occupancy = 0

    for line_idx, line in enumerate(data):

        datapoint = {}

        line = [int(x) for x in line]

        group_id = str(line[0])
        cpu = line[1]
        tid = line[2]
        nanoseconds = line[3]
        tsc = line[4]
        new_time_enabled = line[5]
        new_time_running = line[6]

        datapoint["timestamp"] = tsc
        datapoint["ref_cycles"] = tsc - total_tsc
        total_tsc = tsc

        # Special handling for group-unrelated counters via RDT
        if rdt:
            local_mem_traffic = line[7]
            l3_occupancy = line[8]
            datapoint["local_mem_traffic"] = local_mem_traffic - last_local_mem_traffic
            datapoint["l3_occupancy"] = l3_occupancy - last_l3_occupancy
            last_local_mem_traffic = local_mem_traffic
            last_l3_occupancy = l3_occupancy

        if group_id not in totals: # first time around

            time_running, time_enabled = new_time_running, new_time_enabled

            totals[group_id]["time_enabled"] = time_enabled
            totals[group_id]["time_running"] = time_running

            multiplier = float(time_running) / float(time_enabled)
            assert(multiplier >= 0 and multiplier <= 1)
            #multiplier = 1.0

            for event, idx_in_line in groups[group_id].items():
                cur = line[idx_in_line + non_group_ctrs]
                datapoint[event] = cur * (1 / multiplier)
                totals[group_id][event] = cur

        else:

            assert(new_time_enabled > totals[group_id]["time_enabled"])
            assert(new_time_running > totals[group_id]["time_running"])
            time_enabled = new_time_enabled - totals[group_id]["time_enabled"]
            time_running = new_time_running - totals[group_id]["time_running"]

            totals[group_id]["time_enabled"] = time_enabled
            totals[group_id]["time_running"] = time_running

            multiplier = float(time_running) / float(time_enabled)
            assert(multiplier >= 0 and multiplier <= 1)
            #multiplier = 1.0

            for event, idx_in_line in groups[group_id].items():
                cur = line[idx_in_line + non_group_ctrs]
                old = totals[group_id][event]
                datapoint[event] = (cur - old) * (1 / multiplier)

                totals[group_id][event] = cur

        # create a csv row
        row = [ float("nan") ] * len(columns)
        for event, count in datapoint.items():
            row[column_idxs[event]] = count

        rows.append(row)

    # No file extension substitution
    output_filename = filename + ".csv"
    output_file = open(output_filename, "w")

    output_file.write(",".join(columns) + "\n")
    for row in rows:
        output_file.write(",".join([str(x) for x in row]) + "\n")

    output_file.close()
    return output_filename






if __name__ == "__main__":
    assert(len(sys.argv) == 2)
    filename = sys.argv[1]
    to_csv(filename)
