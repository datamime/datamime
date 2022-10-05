#/bin/bash

# Define your gcc-4.8 and g++-4.8 installations here
PATH_TO_CC4=/usr/bin/gcc-4.8
PATH_TO_CXX4=/usr/bin/g++-4.8

# Install Xapian core if not already present
if [ ! -d xapian-core-1.2.13 ]
then
    tar -xf xapian-core-1.2.13.tar.gz
    cd xapian-core-1.2.13
    mkdir install
    ./configure --prefix=$PWD/install CXX=$PATH_TO_CXX4 CC=$PATH_TO_CC4
    make -j16
    make install
    cd -
fi

# Build search engine
make CXX=/usr/bin/g++-4.8 
