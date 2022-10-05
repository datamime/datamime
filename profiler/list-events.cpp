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
#include <err.h>
#include <cstring>
#include <perfmon/pfmlib.h>
#include <iostream>
#include <cassert>
#include <stdio.h>
#include <string>
#include <vector>

extern "C" {
#include "perf_util.h"
}

const int MAX_EVENTS_TOGETHER = 5;

const auto os = PFM_OS_PERF_EVENT_EXT;

struct PerfEvent {
    std::string name;
    std::string equiv;
    std::vector<std::string> attributes;
    PerfEvent(const char* _name, const char* _equiv) : name(_name) {
        if (_equiv) equiv = _equiv;
    }
};

perf_event_desc_t* globFds;

const char* attr_type_string(pfm_attr_t type){
    switch(type){
        case PFM_ATTR_UMASK:
            return "PFM_ATTR_UMASK";
        case PFM_ATTR_MOD_BOOL:
            return "PFM_ATTR_MOD_BOOL";
        case PFM_ATTR_MOD_INTEGER:
            return "PFM_ATTR_MOD_INTEGER";
        default:
            errx(1, "[SAGE BUNDLER] unexpected attribute type encountered");
            return nullptr;
    }
}

void get_pmu_events(std::vector<PerfEvent>& events, pfm_pmu_info_t& pinfo){

    pfm_event_info_t einfo;
    einfo.size = sizeof(einfo);

    for(int eidx = pinfo.first_event; eidx != -1;
            eidx = pfm_get_event_next(eidx)){

        if(pfm_get_event_info(eidx, os, &einfo) != PFM_SUCCESS){
            errx(1, "[SAGE BUNDLER] cannot get event info");
        }


        events.emplace_back(PerfEvent(einfo.name, einfo.equiv));
        auto& event = events.back();

        pfm_event_attr_info_t ainfo;
        ainfo.size = sizeof(ainfo);

        for(int aidx = 0; aidx < einfo.nattrs; aidx++){
            if(pfm_get_event_attr_info(eidx, aidx, os, &ainfo) != PFM_SUCCESS){
                errx(1, "[SAGE BUNDLER] cannot get attribute info");
            }
            event.attributes.push_back(ainfo.name);
        }
    }
}

std::vector<PerfEvent> get_all_available_pmu_events(){

    std::vector<PerfEvent> events;

    int i;
    pfm_pmu_info_t pinfo;
    pinfo.size = sizeof(pfm_pmu_info_t); // NOTE: very important incantation!

    pfm_for_all_pmus(i){
        auto pmu = static_cast<pfm_pmu_t>(i);
        auto ret = pfm_get_pmu_info(pmu, &pinfo);
        if(ret == 0 && pinfo.is_present){
            get_pmu_events(events, pinfo);
        }
    }

    return events;
}


int main(int argc, char** argv){

    if(pfm_initialize() != 0){
        errx(1, "[SAGE BUNDLER] pfm_initialize() failed");
    }

    auto events = get_all_available_pmu_events();
    std::cout << "[SAGE BUNDLER] recovered " << events.size() << " events\n";

    for(const auto& event : events){
        std::cout << event.name << std::endl;
        std::cout << "equiv: " << event.equiv << std::endl;
        for(const auto& attr : event.attributes){
            std::cout << "\t" << attr << std::endl;
        }
    }

}
