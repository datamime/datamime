# Flags for git libpfm
CFLAGS += -g -O3 -pthread 

CPPFLAGS = -std=c++11 $(CFLAGS)

CC = gcc
CXX = g++

default: microbenchmark random-iaxpy traverse-array traverse-array-mt
all: microbenchmark random-iaxpy traverse-array traverse-array-mt

microbenchmark:
	$(CXX) $(CPPFLAGS) -o microbenchmark microbenchmark.cpp

random-iaxpy:
	$(CXX) $(CPPFLAGS) -o random-iaxpy random-iaxpy.cpp

traverse-array.o:
	$(CXX) $(CPPFLAGS) -o traverse-array.o -c traverse-array.cpp

traverse-array: traverse-array.o
	$(CXX) $(CPPFLAGS) -o traverse-array traverse-array.o

traverse-array-mt:
	$(CXX) $(CPPFLAGS) -o traverse-array-mt traverse-array-mt.cpp

clean:
	rm -f *.o microbenchmark random-iaxpy traverse-array traverse-array-mt
