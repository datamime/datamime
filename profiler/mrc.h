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
#ifndef __MRC_H__
#define __MRC_H__
#include <string>

// Paths to Intel's Cache Allocation Technology CBM and COS tools
const std::string CAT_CBM_TOOL_DIR = "./lltools/build/cat_cbm -c ";
const std::string CAT_COS_TOOL_DIR = "./lltools/build/cat_cos -c ";

//Logging, monitoring and profiling vars
const bool enableLogging(true); //Turn on for detailed logging of profiling

#endif // __MRC_H__
