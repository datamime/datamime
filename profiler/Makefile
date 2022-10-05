DIR := ${CURDIR}

LIBPFMPATH = ${DIR}/lib/libpfm-4.10.1
LLTOOLSPATH = ${DIR}/lltools
TOOLSPATH = ${DIR}/tools
BUILDPATH = ${DIR}/build

PU_SRC = $(LIBPFMPATH)/perf_examples/perf_util.c

# Path to Armadillo installation
ARMADILLO_PATH = /data/sanchez/tools/hrlee/armadillo-10.8.2/install

# Flags for git libpfm
LDFLAGS += -L$(LIBPFMPATH)/lib -lpfm -lnuma -L$(ARMADILLO_PATH)/lib \
		   -Wl,-rpath=$(LIBPFMPATH)/lib -Wl,--disable-new-dtags 
CFLAGS += -g -O3 -I$(LIBPFMPATH)/include -I$(LIBPFMPATH)/perf_examples \
		  -DCONFIG_PFMLIB_DEBUG -DCONFIG_PFMLIB_OS_LINUX -I. -D_GNU_SOURCE \
		  -pthread -I$(LLTOOLSPATH)/include -I$(ARMADILLO_PATH)/include

CPPFLAGS = -std=c++17 $(CFLAGS)
CPPFLAGS += -DELPP_NO_DEFAULT_LOG_FILE

CC = gcc
CXX = g++

default: datamime-profiler list-events init-cat-cbm tsc-freq

error:
	# Please set LIBPFMPATH at the top of this file!

perf_util.o:
	$(CC) $(CFLAGS) -o ${BUILDPATH}/perf_util.o -c $(PU_SRC)

datamime-profiler.o: datamime-profiler.cpp
	$(CXX) $(CPPFLAGS) -o ${BUILDPATH}/datamime-profiler.o -c datamime-profiler.cpp

easylogging++.o: easylogging++.cpp
	$(CXX) $(CPPFLAGS) -o ${BUILDPATH}/easylogging++.o -c easylogging++.cpp

cache_utils.o:
	$(CXX) $(CPPFLAGS) -o ${BUILDPATH}/cache_utils.o -c cache_utils.cpp

list-events.o:
	$(CXX) $(CPPFLAGS) -o ${BUILDPATH}/list-events.o -c list-events.cpp

tsc-freq: tsc-freq.cpp
	$(CXX) $(CPPFLAGS) -o $(TOOLSPATH)/tsc-freq tsc-freq.cpp

init-cat-cbm: cache_utils.o
	$(CXX) $(CPPFLAGS) -o $(TOOLSPATH)/init-cat-cbm init-cat-cbm.cpp ${BUILDPATH}/cache_utils.o $(LDFLAGS)

list-events: list-events.o perf_util.o
	$(CXX) $(CPPFLAGS) -o $(TOOLSPATH)/list-events ${BUILDPATH}/perf_util.o ${BUILDPATH}/list-events.o $(LDFLAGS)

datamime-profiler: datamime-profiler.o perf_util.o cache_utils.o easylogging++.o
	$(CXX) $(CPPFLAGS) -o datamime-profiler ${BUILDPATH}/perf_util.o ${BUILDPATH}/datamime-profiler.o ${BUILDPATH}/cache_utils.o ${BUILDPATH}/easylogging++.o $(LDFLAGS)

clean:
	rm -f ${BUILDPATH}/* datamime-profiler $(TOOLSPATH)/*

# Make lltools, download and make libpfm
init:
	mkdir -p lib
	mkdir -p output
	mkdir -p ${BUILDPATH}
	mkdir -p ${TOOLSPATH}
	wget -O ./lib/download http://sourceforge.net/projects/perfmon2/files/libpfm4/libpfm-4.10.1.tar.gz/download
	tar -xvf lib/download -C lib
	make -C $(LIBPFMPATH)
	rm lib/download
