CC=g++
CXX=g++
RANLIB=ranlib

LIBSRC= MapReduceFramework.cpp Barrier.h Barrier.cpp
LIBOBJ=$(LIBSRC:.cpp=.o)

INCS=-I.
CFLAGS = -Wall -std=c++11 -g -pthread -lrt $(INCS)
CXXFLAGS = -Wall -std=c++11 -g -pthread -lrt $(INCS)

LIB = libMapReduceFramework.a
TARGETS = $(LIB)

TAR=tar
TARFLAGS=-cvf
TARNAME=ex3.tar
TARSRCS=$(LIBSRC) Makefile README

all: $(TARGETS)

$(TARGETS): $(LIBOBJ)
	$(AR) $(ARFLAGS) $@ $^
	$(RANLIB) $@


depend:
	makedepend -- $(CFLAGS) -- $(SRC) $(LIBSRC)

tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)
