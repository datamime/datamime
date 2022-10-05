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
#include "cache_utils.h"
#include <unistd.h>

int main(int argc, char *[]) {
    int cache_line_size = (int)sysconf(_SC_LEVEL3_CACHE_LINESIZE);
    int num_logical_cores = (int)sysconf(_SC_NPROCESSORS_CONF);
    int cache_num_ways = (int)sysconf(_SC_LEVEL3_CACHE_ASSOC);

    printf("L3 cache line size: %d\n", cache_line_size);
    printf("L3 cache associativity: %d\n", cache_num_ways);

    cache_utils::share_all_cache_ways(num_logical_cores, cache_num_ways);
}

