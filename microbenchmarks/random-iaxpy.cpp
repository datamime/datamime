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
#include <iostream>
#include <string>
#include <assert.h>
#include <thread>
#include <random>

void random_iaxpy(int array_size_in_MB) {
    std::vector<uint64_t> src((array_size_in_MB*1e6)/64);
    std::vector<uint64_t> dst((array_size_in_MB*1e6)/64);
    std::fill(src.begin(), src.end(), 1ul);
    std::fill(dst.begin(), dst.end(), 1ul);
    uint64_t src_size = (uint64_t)(src.size());
    uint64_t iter = 0;
    while (1) {
        // Access elements pseudo-randomly to induce as many cache misses as possible.
        uint64_t idx = iter*(iter + 3) % src_size;
        dst[idx] = dst[idx] + src[idx]*iter;
        if (++iter % 1000000000 == 0)
            printf("Completed 1B iterations of iaxpy\n");
    }
}

int main(int argc, char* argv[]){
    if(argc < 2){
        std::cout << "Usage: ./random-iaxpy <array_size_in_MB>\n";
        return -2;
    }

    std::srand(std::time(0));

    int array_size_in_MB = std::stoi(argv[1]);

    std::vector<std::thread> thrs;

    for (int i = 0; i < 4; i++) {
        thrs.emplace_back(std::thread(random_iaxpy, array_size_in_MB));
    }

    int counter;
    while(true){
        counter++;
    }

    return 0;
}
