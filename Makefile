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
extlibs_tgt=$(lib_dir)/pcm $(lib_dir)/papi
extlibs_ln=-lpcm -lpapi -lrocm_smi64
extlibs_incl=-I$(lib_dir)/pcm -I$(lib_dir)/papi/include
extlibs_dirs=-L$(lib_dir)/pcm -L$(lib_dir)/papi/lib -L$(lib_dir)/rocm_smi/lib
export PAPI_ROCMSMI_ROOT=$(shell pwd)/$(lib_dir)/rocm_smi
export LD_RUN_PATH=$(lib_dir)/pcm:$(lib_dir)/papi/lib:$(lib_dir)/rocm_smi/lib

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

# cmake
CMAKE=cmake

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
	@rm -rf $@
	cd $(lib_dir) && git clone https://github.com/opcm/pcm.git $(@F)
	$(MAKE) -C $@ -j $(nprocs)

lib/papi: lib/rocm_smi | $(lib_dir)
	@rm -rf $@
	cd $(lib_dir) && git clone https://bitbucket.org/icl/papi.git $(@F)
	installdir=$(shell pwd)/$@ && \
		cd $@/src && \
		./configure --prefix=$$installdir --with-components="rapl rocm_smi"
	$(MAKE) -C $@/src -j $(nprocs)
	$(MAKE) -C $@/src install

lib/rocm_smi: | $(lib_dir)
	@rm -rf $@
	cd $(lib_dir) && git clone https://github.com/RadeonOpenCompute/rocm_smi_lib.git $(@F);
	installdir=$(shell pwd)/$@ && \
		cd $@ && mkdir -p build && cd build && \
		$(CMAKE) -DCMAKE_INSTALL_PREFIX=$$installdir -Wdev -Wdeprecated -S ..
	$(MAKE) -C $@/build -j $(nprocs)
	$(MAKE) -C $@/build install

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
