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

//#define ARMA_DONT_USE_CXX11
#define NOMINMAX
#include <armadillo>

// Local specializations
typedef arma::Col<uint64_t> u64vec;
typedef arma::Col<uint32_t> u32vec;
typedef arma::vec dblvec;
typedef arma::Mat<uint64_t> u64mat;
typedef arma::Mat<uint32_t> u32mat;
typedef arma::Mat<double> dblmat;
typedef arma::Cube<uint64_t> u64cube;
typedef arma::Cube<uint32_t> u32cube;
typedef arma::Cube<double> dblcube;
