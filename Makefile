# get number of processors
ifeq ($(shell uname -s),Linux)
nprocs=$(shell grep -c ^processor /proc/cpuinfo)
else
nprocs=1
endif

# directories
src_dir=src
tgt_dir=bin
lib_dir=lib
obj_dir=obj
dep_dir=$(obj_dir)/.deps

# external libs
extlibs=pcm
extlibs_ln=-lPCM
extlibs_tgt=$(addprefix lib/,$(extlibs))

# files
src=$(wildcard src/*.cpp)
obj=$(patsubst $(src_dir)/%.cpp,$(obj_dir)/%.o,$(src))
deps=$(patsubst $(src_dir)/%.cpp,$(dep_dir)/%.d,$(src))
tgt=$(tgt_dir)/profiler

# compiler flags
cc=g++
DEBUG?=
warn=-Wall -Wextra -Wno-unknown-pragmas
ifeq ($(DEBUG),true)
cflags=$(warn) -g -O0
else
cflags=$(warn) -O3 -D NDEBUG
endif
cppflags=-std=c++17
incl=$(addprefix -I,$(extlibs_tgt))
libs=-pthread -lbfd -ldwarf $(extlibs_ln)
libdirs=$(addprefix -L,$(extlibs_tgt))

# rules -----------------------------------------------------------------------

default: all

.PHONY: all
all: libs $(tgt)

.PHONY: libs
libs: $(extlibs_tgt)

$(tgt_dir):
	mkdir -p $@
$(obj_dir):
	mkdir -p $@
$(dep_dir):
	mkdir -p $@
$(lib_dir):
	mkdir -p $@

lib/pcm: | $(lib_dir)
	cd $(lib_dir) && git clone https://github.com/opcm/pcm.git $(@F)
	$(MAKE) -C $@ -j $(nprocs)

$(tgt): $(obj) | $(tgt_dir)
	$(cc) $^ $(libs) $(libdirs) -o $@

$(obj_dir)/%.o: $(src_dir)/%.cpp $(dep_dir)/%.d | $(obj_dir) $(dep_dir)
	$(cc) -MT $@ -MMD -MP -MF $(dep_dir)/$*.d \
		$(cflags) $(cppflags) $(incl) -c -o $@ $<

$(deps):

include $(wildcard $(deps))

.PHONY: remake
remake: clean all

# do not clean libraries because
# those may take a while to rebuild
.PHONY: clean
clean:
	rm -rf $(tgt_dir) $(obj_dir)

# clean everything, including libraries
.PHONY: purge
purge: clean
	rm -rf $(lib_dir)
