/** $lic$
 * Copyright (C) 2021-2022 by Massachusetts Institute of Technology
 *
 * This file is part of Datamime.
 *
 * This tool is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 3.
 *
 * If you use this software in your research, we request that you reference
 * the Datamime paper ("Datamime: Generating Representative Benchmarks by
 * Automatically Synthesizing Datasets", Lee and Sanchez, MICRO-55, October 2022)
 * as the source in any publications that use this software, and that you send
 * us a citation of your work.
 *
 * This tool is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ctime>
#include <iostream>
#include <vector>
#include <random>
#include <thread>

// aligned at 16 byte boundary => double the memory pressure
#ifdef ALIGN16
struct Elem {
    int64_t val;
} __attribute__ ((aligned (8)));

// struct Elem {
//     int64_t val;
// } __attribute__ ((aligned (16)));

#else
struct Elem {
    int32_t val;
} __attribute__ ((aligned (4)));

// struct Elem {
//     int64_t val;
// } __attribute__ ((aligned (8)));
#endif

void usage(char* argv[]) {
    std::cout << "USAGE:" << std::endl;
    std::cout << argv[0] << " -s <array-size-in-kB> -i <num-iters> [-v] [-h]" \
        << std::endl;
    std::cout << "\t-s <array-size-in-kB> : size of array to traverse" \
        << std::endl;
    std::cout << "\t-i <num-iters> : Each iteration traverses array once" \
        << std::endl;
    std::cout << "\t-n <num-threads> : Number of worker threads" \
        << std::endl;
    std::cout << "\t-v : Verbose output" << std::endl;
    std::cout << "\t-h : Print this help message" << std::endl;
}

// Return ms
double duration(const struct timespec& begin, const struct timespec& end) {
    double elapsed = (end.tv_sec - begin.tv_sec) * 1e3;
    elapsed += (end.tv_nsec - begin.tv_nsec) * 1e-6;
    return elapsed;
}

void traverse_array(int arrayKBs, int64_t iters) {
    // Create array of specified size
    size_t arrayElems = arrayKBs * 1024ul / sizeof(Elem);

    volatile Elem* array = new volatile Elem[arrayElems];

    for (int64_t i = 0; i < iters; i++) {
        for (int64_t j = 0; j < arrayElems; ++j) {
            // NOTE: + is much cheaper than / and so tends to make this more bw
            // intensive
            // array[j].val += 5;
            array[j].val /= 5;
            // sum += array[j].val;
        }
    }

    delete[] array;
}

int main(int argc, char* argv[]) {
    int c;
    int arrayKBs = -1;
    int64_t iters = -1;
    int nthreads = 1;
    bool verbose = false;

    while ((c = getopt(argc, argv, ":s:i:n:vh")) != -1) {
        switch(c) {
            case 's':
                arrayKBs = atoi(optarg);
                break;
            case 'i':
                iters = atoll(optarg);
                break;
            case 'n':
                nthreads = atoi(optarg);
                break;
            case 'v':
                verbose = true;
                break;
            case 'h':
                usage(argv);
                return 0;
                break;
            case '?':
                usage(argv);
                return -1;
                break;
            case ':':
                usage(argv);
                return -1;
                break;
        }
    }

    if ((arrayKBs <= 0) || (iters <= 0)) {
        usage(argv);
        return -1;
    }

    struct timespec beginInit, begin, end;

    // Create array of specified size
    size_t arrayElems = arrayKBs * 1024ul / sizeof(Elem);
    std::cout << "Array size : " << arrayKBs << " kB | # Elements : " \
        << arrayElems << " | iters : " << iters << std::endl;

    // Begin init time
    clock_gettime(CLOCK_REALTIME, &beginInit);

    volatile Elem* array = new volatile Elem[arrayElems];

    // Begin time
    clock_gettime(CLOCK_REALTIME, &begin);

    if (verbose) {
        double elapsedInit = duration(beginInit, begin);
        std::cout << "Init Time = " << elapsedInit << " msec" << std::endl;
    }

    std::vector<std::thread> thrs;

    for (int i = 0; i < nthreads; i++) {
        thrs.emplace_back(std::thread(traverse_array, arrayKBs, iters));
    }

    //Traverse array
    for (int64_t i = 0; i < iters; i++) {
        for (int64_t j = 0; j < arrayElems; ++j) {
            // NOTE: + is much cheaper than / and so tends to make this more bw
            // intensive
            // array[j].val += 5;
            array[j].val /= 5;
            // sum += array[j].val;
        }
    }

    for (std::thread &thr : thrs) {
        thr.join();
    }

    // End time
    clock_gettime(CLOCK_REALTIME, &end);

    if (verbose) {
        double elapsed = duration(begin, end);
        std::cout << "Elapsed Time = " << elapsed << " msec" << std::endl;
    }

    delete[] array;

    return 0;
}
