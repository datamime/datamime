# $lic$
# Copyright (c) 2021-2022 by Massachusetts Institute of Technology
#
# This file is part of Datamime.
#
# This tool is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation, version 3.
#
# If you use this software in your research, we request that you reference
# the Datamime paper ("Datamime: Generating Representative Benchmarks by
# Automatically Synthesizing Datasets", Lee and Sanchez, MICRO-55, October 2022)
# as the source in any publications that use this software, and that you send
# us a citation of your work.
#
# This tool is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program. If not, see <http://www.gnu.org/licenses/>.
# $endlic$
from decimal import Decimal

# Defines distributions used by mutilate to drive memcached

# Quantize to second decimal place
def quantize(val):
    return float(Decimal(float(val)).quantize(Decimal('1.00')))

class Distribution:
    def argstr(self):
        raise NotImplementedError("Method needs implementation")

# quantize to 100th of an integer
class Fixed(Distribution):
    def __init__(self, value, isint = False):
        if isint:
            self.value = int(value)
        else:
            self.value = quantize(value)

    # Generate String acceptable as command line arg for mutilate
    def argstr(self):
        return "{}".format(self.value)

class Uniform(Distribution):
    def __init__(self, maximum):
        self.value = quantize(maximum)

    # Generate String acceptable as command line arg for mutilate
    def argstr(self):
        return "uniform:{}".format(self.maximum)

class Normal(Distribution):
    def __init__(self, mean, sd):
        self.mean = quantize(mean)
        self.sd = quantize(sd)

    # Generate String acceptable as command line arg for mutilate
    def argstr(self):
        return "normal:{},{}".format(self.mean,self.sd)

class Exponential(Distribution):
    def __init__(self, lmbda):
        self.lmbda = quantize(lmbda)

    # Generate String acceptable as command line arg for mutilate
    def argstr(self):
        return "exponential:{}".format(self.exponential)

