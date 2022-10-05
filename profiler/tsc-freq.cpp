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
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <iostream>

class CPUID {
  uint32_t regs[4];

public:
  explicit CPUID(uint32_t eax, uint32_t ecx) {
    asm volatile("cpuid"
                 : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
                 : "a"(eax), "c"(ecx));
  }

  const uint32_t &EAX() const { return regs[0]; }
  const uint32_t &EBX() const { return regs[1]; }
  const uint32_t &ECX() const { return regs[2]; }
  const uint32_t &EDX() const { return regs[3]; }
};

int main(int argc, char* argv[]) {
    int32_t core_freq_mhz, eax, ecx;

    if (argc != 4) {
        printf("Usage: %s [core_crystal_freq] [eax] [ecx]\n", argv[0]);
        printf("Assuming core crystal freq = 25MHz, eax=0x15 and ecx=0x0\n "
                "to derive TSC frequency.\n");
        printf("Consult Intel Software Manual Vol.3 18.7.3 to find out\n "
               "the core crystal clock frequency and eax/ecx registers\n "
               "needed to calculate the tsc frequency for your system\n");

        core_freq_mhz = 25;
        eax = 21;
        ecx = 0;
    }
    else {
        core_freq_mhz = atoi(argv[1]);
        std::stringstream ss2;
        ss2 << std::hex << argv[2];
        ss2 >> eax;
        std::stringstream ss3;
        ss3 << std::hex << argv[3];
        ss3 >> ecx;
    }

    CPUID tsc_info(eax, ecx);

    if (tsc_info.EAX() == 0 || tsc_info.EBX() == 0) {
        printf("Processor architecture does not support obtaining TSC frequency through CPUID\n");
        return -1;
    }

    int32_t tsc_freq, tsc_freq_alt;
    if (tsc_info.ECX() != 0) {
        tsc_freq = (tsc_info.ECX() * tsc_info.EBX()) / tsc_info.EAX();
        printf("tsc_freq_hz = %d\n", tsc_freq);
    } else {
        // Assume 25MHz Nominal core crystal clock frequency
        // TODO: dynamically find out according to architecture as specified in
        // Intel Software Manual Vol.3 18.7.3
        tsc_freq = (core_freq_mhz * tsc_info.EBX()) / tsc_info.EAX();
        printf("tsc_freq_mhz = %d\n", tsc_freq);
    }
}

