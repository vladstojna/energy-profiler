# get number of processors
ifeq ($(shell uname -s),Linux)
nprocs := $(shell grep -c ^processor /proc/cpuinfo)
else
nprocs := 1
endif

# directories
src_dir := src
tgt_dir := bin
lib_dir := lib
obj_dir := obj
dep_dir := $(obj_dir)/.deps

# external libs
extlibs_tgt  := $(addprefix $(lib_dir)/, pcm papi rocm_smi pugixml)
extlibs_incl := $(addprefix $(lib_dir)/, pcm papi/include rocm_smi/include pugixml/include)
extlibs_dirs := $(addprefix $(lib_dir)/, pcm papi/lib rocm_smi/lib pugixml/lib)

export PAPI_ROCMSMI_ROOT=$(shell pwd)/$(lib_dir)/rocm_smi
export LD_RUN_PATH=$(lib_dir)/pcm:$(lib_dir)/papi/lib:$(lib_dir)/rocm_smi/lib

# versions
pugixml_ver := 1.11.4
papi_ver    := 6-0-0-1

# files
src  := $(wildcard src/*.cpp)
obj  := $(patsubst $(src_dir)/%.cpp, $(obj_dir)/%.o, $(src))
deps := $(patsubst $(src_dir)/%.cpp, $(dep_dir)/%.d, $(src))
tgt  := $(tgt_dir)/profiler

# compiler flags
cc := g++
cppstd := c++17
DEBUG ?=
cflags := -Wall -Wextra -Wno-unknown-pragmas -fPIE -g
ifeq ($(DEBUG),true)
cflags += -O0
else
cflags += -O3 -DNDEBUG
endif
cflags += $(addprefix -I, $(extlibs_incl))
cflags += -std=$(cppstd)

# linked flags
ldflags := -pthread -lbfd -ldwarf -lpcm -lpapi -lrocm_smi64 -lpugixml
ldflags += $(addprefix -L, $(extlibs_dirs))

# cmake
CMAKE := cmake

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
	# download the release and extract the archive
	cd $(lib_dir) && \
		wget https://bitbucket.org/icl/papi/get/papi-$(papi_ver)-t.tar.gz && \
		tar xf papi-$(papi_ver)-t.tar.gz --one-top-level=$(@F) --strip-components=1 && \
		rm -f papi-$(papi_ver)-t.tar.gz
	# build
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

lib/pugixml: | $(lib_dir)
	@rm -rf $@
	# download the release and extract the archive
	cd $(lib_dir) && \
		wget https://github.com/zeux/pugixml/releases/download/v$(pugixml_ver)/pugixml-$(pugixml_ver).tar.gz && \
		tar xf pugixml-$(pugixml_ver).tar.gz --one-top-level=$(@F) --strip-components=1 && \
		rm -f pugixml-$(pugixml_ver).tar.gz
	# build
	installdir=$(shell pwd)/$@ && \
		cd $@ && mkdir -p build && \
		$(CMAKE) -DCMAKE_INSTALL_PREFIX=$$installdir -Wdev -Wdeprecated -S . -B build
	$(MAKE) -j $(nprocs) -C $@/build install

$(tgt): $(obj) | $(tgt_dir)
	$(cc) $^ $(ldflags) -o $@

$(obj_dir)/%.o: $(src_dir)/%.cpp $(dep_dir)/%.d | $(obj_dir) $(dep_dir)
	$(cc) -MT $@ -MMD -MP -MF $(dep_dir)/$*.d $(cflags) -c -o $@ $<

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
