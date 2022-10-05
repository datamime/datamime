#!/bin/bash
# Requirements: gcc/g++ supporting CXX17 standard

# Download and unpack libtorch if not already present
if [ ! -d libtorch ]
then
    if ! test -f "libtorch-cxx11-abi-shared-with-deps-1.10.2+cpu.zip"; then
        wget https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-1.10.2%2Bcpu.zip
    fi
    unzip libtorch-cxx11-abi-shared-with-deps-1.10.2+cpu.zip
fi

libtorchpath=${PWD}/libtorch 

mkdir -p build
cd build
cmake -DCMAKE_PREFIX_PATH=${libtorchpath} ..
cmake --build . --config Release
make
