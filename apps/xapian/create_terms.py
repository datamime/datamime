#!/usr/bin/env python

# Script to create search terms with occurance limit between 100 and 10000 given a
# Xapian DB
# genTerms saves to the destination in the third argument as follows:
#
# ...
# <term>,<occurances>
# ...
#

import os
import sys
import time
import subprocess

if not len(sys.argv) == 4:
    print("Usage: ./create_terms.py genterms_binpath db_path terms_file_path")

if not os.path.exists(sys.argv[3]):
    print("Creating directories recursively up to {}".format(sys.argv[2]))
    os.makedirs(sys.argv[3])

processes = []
ll = 100
for ul in range(100, 10001, 100):
    if ll < ul:
        cmd = [
            sys.argv[1], "-d",
            sys.argv[2],
            "-f",
            "{}/terms_ul{}.in".format(sys.argv[3], ul),
            "-u", str(ul), "-l", str(ll)]
        processes.append(subprocess.Popen(cmd))
        time.sleep(0.1)

    # Don't go too crazy here.
    if len(processes) >= 10:
        processes[0].wait()
        processes.pop(0)

