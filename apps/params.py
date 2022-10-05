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
# Defines parameters, their bounds, and constraints for each workload.
# Note that constraints should be inequality constraints.

all_params = {
    # Memcached params and constraints
    "memcached": {
        "parameters":
            [
                {
                    "name": "qps",
                    "type": "range",
                    "bounds": [10000, 300000],
                    "value_type": "int",
                    "log_scale": True,
                },
                {
                    "name": "key_mean",
                    "type": "range",
                    "bounds": [10, 150],
                    "value_type": "float",
                },
                {
                    "name": "key_stdev",
                    "type": "range",
                    "bounds": [0.01, 10],
                    "value_type": "float",
                },
                {
                    "name": "val_mean",
                    "type": "range",
                    "bounds": [10, 10000],
                    "value_type": "float",
                    "log_scale": True,
                },
                {
                    "name": "val_stdev",
                    "type": "range",
                    "bounds": [0.01, 100],
                    "value_type": "float",
                    "log_scale": True,
                },
                {
                    "name": "update_ratio",
                    "type": "range",
                    "bounds": [0.0, 1.0],
                    "value_type": "float",
                },
            ],
        "constraints": None
    },

    # Silo params and constraints
    "silo": {
        "parameters":
            [
                {
                    "name": "qps",
                    "type": "range",
                    "bounds": [5000, 100000],
                    "value_type": "int",
                    "log_scale": True,
                },
                {
                    "name": "scale_factor",
                    "type": "range",
                    "bounds": [1, 20],
                    "value_type": "int",
                },
                {
                    "name": "fq_neworder",
                    "type": "range",
                    "bounds": [1, 96],
                    "value_type": "int",
                },
                {
                    "name": "fq_payment",
                    "type": "range",
                    "bounds": [1, 96],
                    "value_type": "int",
                },
                {
                    "name": "fq_delivery",
                    "type": "range",
                    "bounds": [1, 96],
                    "value_type": "int",
                },
                {
                    "name": "fq_orderstatus",
                    "type": "range",
                    "bounds": [1, 96],
                    "value_type": "int",
                },
            ],
        "constraints":
            ["fq_neworder + fq_payment "
             "+ fq_delivery + fq_orderstatus <= 99"]
    },

    # Xapian params and constraints
    "xapian": {
        "parameters":
            [
                {
                    "name": "qps",
                    "type": "range",
                    "bounds": [50, 2000],
                    "value_type": "int",
                    "log_scale": True,
                },
                {
                    "name": "skew",
                    "type": "range",
                    "bounds": [0, 3],
                    "value_type": "float",
                },
                {
                    "name": "terms_ulimit",
                    "type": "range", # Multiples of 100
                    "bounds": [2, 100],
                    "value_type": "int",
                },
                {
                    "name": "avg_doc_len",
                    "type": "range",
                    "bounds": [1, 10], # Multiples of 100
                    "value_type": "int",
                },
            ],
        "constraints": None
    },

    # Dnn params and constraints
    "dnn": {
        "parameters":
            [
                {
                    "name": "qps",
                    "type": "range",
                    "bounds": [5.0, 1000.0],
                    "value_type": "float",
                    "log_scale": True,
                },
                {
                    "name": "conv_layers",
                    "type": "range",
                    "bounds": [1, 100],
                    "value_type": "int",
                },
                {
                    "name": "strided_conv_layers",
                    "type": "range",
                    "bounds": [2, 5],
                    "value_type": "int",
                },
                {
                    "name": "maxpool_layers",
                    "type": "range",
                    "bounds": [1, 5],
                    "value_type": "int",
                },
                {
                    "name": "fc_layers",
                    "type": "range",
                    "bounds": [1, 3],
                    "value_type": "int",
                },
                {
                    "name": "init_channels",
                    "type": "range",
                    "bounds": [3, 64],
                    "value_type": "int",
                },
            ],
        "constraints": ["strided_conv_layers + maxpool_layers <= 6"]
    }
}

