CC=g++
CXX=g++


LIBSRC= VirtualMemory.cpp
LIBOBJ=$(LIBSRC:.cpp=.o)

INCS=-I.
CFLAGS = -Wall -std=c++11 -g  $(INCS)
CXXFLAGS = -Wall -std=c++11 -g  $(INCS)

OSMLIB = libVirtualMemory.a
TARGETS = $(OSMLIB)

TAR=tar
TARFLAGS=-cvf
TARNAME=ex4.tar
TARSRCS= $(LIBSRC) Makefile README

all: $(TARGETS)

$(TARGETS): $(LIBOBJ)
	$(AR) $(ARFLAGS) $@ $^


tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)

clean:
	$(RM) $(TARGETS) $(OSMLIB) $(OBJ) $(LIBOBJ) *~ *core