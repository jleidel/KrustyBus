#
# KrustyBus/Makefile
#
# Copyright (C) 2017-2023 Tactical Computing Laboratories, LLC
# All Rights Reserved
# contact@tactcomplabs.com
#
# See LICENSE in the top level directory for licensing details
#

ifeq (, $(shell which sst-config))
 $(error "No sst-config in $(PATH), add `sst-config` to your PATH")
endif

ELEMENTS_HEADERS=$(shell sst-config SST_ELEMENT_LIBRARY SST_ELEMENT_LIBRARY_HOME)/include

CXX=$(shell sst-config --CXX)
CXXFLAGS=$(shell sst-config --ELEMENT_CXXFLAGS) -I$(ELEMENTS_HEADERS)
LDFLAGS=$(shell sst-config --ELEMENT_LDFLAGS)
CPPFLAGS=-I./
OPTIMIZE_FLAGS=-O2

COMPONENT_LIB=libKrustyBus.so

KRUSTYBUS_SOURCES := $(wildcard *.cc)

KRUSTYBUS_HEADERS := $(wildcard *.h)

KRUSTYBUS_OBJS := $(patsubst %.cc,%.o,$(wildcard *.cc))

KRUSTYBUS_INCS := $(patsubst %.py,%.inc,$(wildcard *.py))

all: $(COMPONENT_LIB)

sanity: OPTIMIZE_FLAGS = -O1 -g -Wall -fsanitize=address
sanity: $(COMPONENT_LIB)

debug: CXXFLAGS += -DENABLE_SSTDBG -DSSTDBG_MPI -DDEBUG -g -Wall
debug: $(COMPONENT_LIB)

$(COMPONENT_LIB): $(KRUSTYBUS_OBJS)
	$(CXX) $(OPTIMIZE_FLAGS) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ *.o
%.o:%.cc $(KRUSTYBUS_HEADERS)
	$(CXX) $(OPTIMIZE_FLAGS) $(CXXFLAGS) $(CPPFLAGS) -c $<
%.inc:%.py
	od -v -t x1 < $< | sed -e 's/^[^ ]*[ ]*//g' -e '/^\s*$$/d' -e 's/\([0-9a-f]*\)[ $$]*/0x\1,/g' > $@
install: $(COMPONENT_LIB)
	sst-register KrustyBus KrustyBus_LIBDIR=$(CURDIR)
clean:
	rm -Rf *.o *.inc $(COMPONENT_LIB)

#-- EOF
