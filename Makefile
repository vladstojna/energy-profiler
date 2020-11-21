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
extlibs=pcm papi
extlibs_tgt=$(addprefix lib/,$(extlibs))
extlibs_ln=-l:libPCM.a -l:libpapi.a
extlibs_dirs=-Llib/pcm -Llib/papi/src
extlibs_incl=-Ilib/pcm -Ilib/papi/src

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
incl=$(extlibs_incl)
libs=-pthread -lbfd -ldwarf $(extlibs_ln)
libdirs=$(extlibs_dirs)

# rules -----------------------------------------------------------------------

.PHONY: all libs remake clean purge

default: all

all: libs $(tgt)

libs: $(extlibs_tgt)

$(tgt_dir):
	@mkdir -p $@
$(obj_dir):
	@mkdir -p $@
$(dep_dir):
	@mkdir -p $@
$(lib_dir):
	@mkdir -p $@

lib/pcm: | $(lib_dir)
	cd $(lib_dir) && git clone https://github.com/opcm/pcm.git $(@F)
	$(MAKE) -C $@ -j $(nprocs)

lib/papi: | $(lib_dir)
	cd $(lib_dir) && git clone https://bitbucket.org/icl/papi.git $(@F)
	cd $@/src && ./configure --with-components="rapl"
	$(MAKE) -C $@/src -j $(nprocs)

$(tgt): $(obj) | $(tgt_dir)
	$(cc) $^ $(libs) $(libdirs) -o $@

$(obj_dir)/%.o: $(src_dir)/%.cpp $(dep_dir)/%.d | $(obj_dir) $(dep_dir)
	$(cc) -MT $@ -MMD -MP -MF $(dep_dir)/$*.d \
		$(cflags) $(cppflags) $(incl) -c -o $@ $<

$(deps):

include $(wildcard $(deps))

remake: clean all

# do not clean libraries because
# those may take a while to rebuild
clean:
	rm -rf $(tgt_dir) $(obj_dir)

# clean everything, including libraries
purge: clean
	rm -rf $(lib_dir)
