#!/usr/bin/env python
import sys
import numpy as np

myfile = sys.argv[1]

samples = np.zeros(10000000)
i = 0
with open(myfile, "r") as f:
    for line in f:
        samples[i] = float(line)
        i += 1

print("mean:", np.mean(samples))
print("median:", np.median(samples))
print("stdev:", np.std(samples))
