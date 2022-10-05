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
#pragma once
#include <sstream>
#include <stack>
#include <armadillo>
using namespace arma;

namespace cache_utils {

// Cache sharing-partitioning utility functions, used heavily by KPart
void share_all_cache_ways(int num_logical_cores, int cache_num_ways);

std::string get_cacheways_for_core(int coreIdx);

void print_allocations(uint32_t *allocs, int num_logical_cores);

void apply_partition_plan(std::stack<int> partitions[],
                          int num_logical_cores);

void do_ucp_mrcs(arma::mat mpkiVsWays,
                 int num_logical_cores,
                 int cache_num_ways);

void do_ucp_ipcs(arma::mat ipcVsWays,
                 int num_logical_cores,
                 int cache_num_ways);

void get_maxmarginalutil_ipcs(arma::vec curve, int cur, int parts,
                              double result[]);

void get_maxmarginalutil_mrcs(arma::vec curve, int cur, int parts,
                              double result[]);

std::vector<std::vector<double> > get_wscurves_for_combinedmrcs(
    std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t> > > >
        cluster_bucks,
    arma::mat ipcVsWays,
    int cache_num_ways);

void smoothenIPCs(arma::mat &ipcVsWays);

void smoothenMRCs(arma::mat &mpkiVsWays);

//void verify_intel_cos_issue(std::stack<int> [] & cluster_partitions, uint32_t
//K);
void verify_intel_cos_issue(std::stack<int> *cluster_partitions, uint32_t K);

} // namespace cache_utils
