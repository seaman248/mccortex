ifndef CC
  CC = gcc
endif

#Arguments (all are optional):
# MAXK=31
# DEBUG=1
# CITY_HASH=1   (use CityHash hash function)

# e.g. for WTCHG cluster3
# 1) pass LIB_PATH=/usr/local/lib/ to compile on WTCHG cluster3
# 2) set LD_LIBRARY_PATH=/usr/local/lib/:$(LD_LIBRARY_PATH) before running

# Targets:
#  make
#  make clean
#  make all

# Use bash as shell
SHELL := /bin/bash

BITFIELDS=$(shell echo $$((($(MAXK)+31)/32)))

ifndef MAXK
  BITFIELDS = 1
  MAXK = 31
else
  BITFIELDS=$(shell echo $$((($(MAXK)+31)/32)))
endif

ifeq ($(BITFIELDS),0)
  $(error Invalid MAXK value '$(MAXK)'. Please choose from 31,63,95,..(32*n-1) [default: 31])
endif

MAX_KMER_SIZE=$(shell echo $$(($(BITFIELDS)*32-1)))
MIN_KMER_SIZE=$(shell echo $$(($(MAX_KMER_SIZE)-30)))

ifeq ($(MIN_KMER_SIZE),1)
	MIN_KMER_SIZE=3
endif

# Use City hash instead of lookup3?
ifdef CITY_HASH
	HASH_KEY_FLAGS = -DUSE_CITY_HASH=1
endif

# Library paths
IDIR_GSL_HEADERS = libs/gsl-1.15
IDIR_HTS = libs/htslib/htslib
IDIR_STRS = libs/string_buffer
IDIR_SEQ = libs/seq_file
IDIR_ALIGN = libs/seq-align/src
IDIR_SEQAN = libs/seqan-library-1.4.0/include/
IDIR_HASH = libs/hash_functions

LIB_GSL=libs/gsl-1.15/.libs/libgsl.a
LIB_HTS=libs/htslib/htslib/libhts.a
LIB_ALIGN=libs/seq-align/src/libalign.a
LIB_STRS=libs/string_buffer/libstrbuf.a

# Resolve some issues linking libz:
# 1) pass LIB_PATH=/usr/local/lib/ to compile on WTCHG cluster3
# 2) also set LD_LIBRARY_PATH=/usr/local/lib/:$(LD_LIBRARY_PATH)
ifdef LIB_PATH
	EXTRA_INCS := -I $(LIB_PATH) -L $(LIB_PATH)
endif

INCS=-I src/basic/ -I src/common/ -I $(IDIR_HASH) -I $(IDIR_STRS) -I $(IDIR_HTS) \
     -I $(IDIR_SEQ) -I $(IDIR_ALIGN) -I $(IDIR_GSL_HEADERS) $(EXTRA_INCS)

# Library linking
LIB_OBJS=$(LIB_GSL) $(LIB_STRS) $(LIB_HTS) $(LIB_ALIGN) $(wildcard libs/hash_functions/*.o)
LINK=-lpthread -lz -lm

# -Winit-self -Wmissing-include-dirs
# -Wstrict-aliasing -Wdiv-by-zero -Wunreachable-code
# -Wcast-qual -Wcast-align -Wmissing-noreturn
# -Wwrite-strings -Waggregate-return -Wundef
# -Wshadow -Wconversion -Wshorten-64-to-32 -Woverlength-strings
# -Wenum-compare -Wlogical-op -Wfloat-equal -Wbad-function-cast

# Optimisations tags for testing
OPT_TESTS = -O0 -Wstack-protector -fstack-protector

# If not debugging, add optimisations and -DNDEBUG=1 to turn off assert() calls
ifdef DEBUG
	OPT = $(OPT_TESTS)
	DEBUG_ARGS = -g -ggdb -DDEBUG=1
else
	OPT = -O3 -DNDEBUG=1
	DEBUG_ARGS = 
endif

CFLAGS = -std=c99 -Wall -Wextra \
         -DMAX_KMER_SIZE=$(MAX_KMER_SIZE) -DMIN_KMER_SIZE=$(MIN_KMER_SIZE) \
         -DNUM_BITFIELDS_IN_BKMER=$(BITFIELDS) $(HASH_KEY_FLAGS) $(OPT) $(DEBUG_ARGS)

KMER_OBJDIR=build/k$(MAXK)

# basic objects compile without MAXK
# common objects require MAXK
BASIC_FILES=$(notdir $(wildcard src/basic/*.c))
COMMON_FILES=$(notdir $(wildcard src/common/*.c))

BASIC_OBJS=$(addprefix build/basic/, $(BASIC_FILES:.c=.o))
KMER_OBJS=$(addprefix $(KMER_OBJDIR)/, $(COMMON_FILES:.c=.o))

BASIC_HDRS=$(wildcard src/basic/*.h)
KMER_HDRS=$(wildcard src/common/*.h)

# DEPS are common dependencies that do not need to be re-built per target
DEPS=Makefile bin build/basic $(KMER_OBJDIR) $(LIB_OBJS)

TOOLS=ctx_build ctx_clean ctx_reads ctx_subgraph ctx_intersect ctx_join \
      ctx_thread ctp_view ctx_call ctx_covg ctx_extend ctx_contigs ctx_diverge \
      ctx_unique ctx_place

all: $(TOOLS)

.SUFFIXES: .c .o _k$(MAXK)
.SECONDARY:

build/basic/call_seqan.o: src/basic/call_seqan.cpp src/basic/call_seqan.h | $(DEPS)
	$(CXX) -Wall -Wextra -I $(IDIR_SEQAN) -c src/basic/call_seqan.cpp -o $@

build/basic/%.o: src/basic/%.c $(BASIC_HDRS) | build/basic
	$(CC) $(DEBUG_ARGS) $(CFLAGS) $(INCS) -c $< -o $@

$(KMER_OBJDIR)/%.o: src/common/%.c $(BASIC_HDRS) $(KMER_HDRS) | $(KMER_OBJDIR)
	$(CC) $(DEBUG_ARGS) $(CFLAGS) $(INCS) -c $< -o $@

bin/%_k$(MAXK): src/tools/%.c $(BASIC_OBJS) $(KMER_OBJS) | $(DEPS)
	$(CC) -o $@ $(CFLAGS) $(INCS) $< $(BASIC_OBJS) $(KMER_OBJS) $(LIB_OBJS) $(LINK)
	@echo Sucessfully compiled $@

ctx_build: bin/ctx_build_k$(MAXK)
ctx_clean: bin/ctx_clean_k$(MAXK)
ctx_reads: bin/ctx_reads_k$(MAXK)
ctx_subgraph: bin/ctx_subgraph_k$(MAXK)
ctx_intersect: bin/ctx_intersect_k$(MAXK)
ctx_join: bin/ctx_join_k$(MAXK)
ctx_thread: bin/ctx_thread_k$(MAXK)
ctp_view: bin/ctp_view_k$(MAXK)
ctx_call: bin/ctx_call_k$(MAXK)
ctx_covg: bin/ctx_covg_k$(MAXK)
ctx_extend: bin/ctx_extend_k$(MAXK)
ctx_contigs: bin/ctx_contigs_k$(MAXK)
ctx_diverge: bin/ctx_diverge_k$(MAXK)

ctx_unique: bin/ctx_unique
bin/ctx_unique:  src/tools/ctx_unique.c $(BASIC_OBJS) | $(DEPS)
	$(CC) -o bin/ctx_unique $(CFLAGS) $(INCS) src/tools/ctx_unique.c $(BASIC_OBJS) $(LIB_OBJS) $(LINK)
	@echo Sucessfully compiled ctx_unique

ctx_place: bin/ctx_place
bin/ctx_place: src/tools/ctx_place.c $(BASIC_OBJS) build/basic/call_seqan.o | $(DEPS)
	$(CC) -o bin/ctx_place $(CFLAGS) $(INCS) src/tools/ctx_place.c $(BASIC_OBJS) build/basic/call_seqan.o $(LIB_OBJS) $(LINK) -lstdc++
	@echo Sucessfully compiled ctx_place

# directories
bin:
	mkdir -p bin

build/basic:
	mkdir -p build/basic

$(KMER_OBJDIR):
	mkdir -p $(KMER_OBJDIR)

# libraries
$(LIB_OBJS):
libs/string_buffer/string_buffer.h:
	cd libs; make

clean:
	rm -rf bin build

.PHONY: all clean $(TOOLS)
