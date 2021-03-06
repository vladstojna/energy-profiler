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
extlibs_tgt  := $(addprefix $(lib_dir)/, pugixml json)
extlibs_incl := $(addprefix $(lib_dir)/, pugixml/include json/single_include)
extlibs_dirs := $(addprefix $(lib_dir)/, pugixml/lib)

# versions
pugixml_ver := 1.11.4
json_ver := 3.9.1

# files
src  := $(wildcard src/*.cpp)
obj  := $(patsubst $(src_dir)/%.cpp, $(obj_dir)/%.o, $(src))
deps := $(patsubst $(src_dir)/%.cpp, $(dep_dir)/%.d, $(src))
tgt  := $(tgt_dir)/profiler

DEBUG ?=

# compiler flags
cc := g++
std := c++17
override cpp +=

cflags := -Wall -Wextra -Wno-unknown-pragmas -Wpedantic -fPIE -g
cflags += $(addprefix -I, $(extlibs_incl))
cflags += $(addprefix -I, include nrg/include)
cflags += -std=$(std)
cflags += $(addprefix -D, $(cpp))

ifdef DEBUG
cflags += -O0
else
cflags += -O3 -DNDEBUG
endif

# linker flags
ldflags := -pthread -lpugixml -lnrg -lstdc++fs
ldflags += $(addprefix -L, $(extlibs_dirs) nrg/lib)

ifneq (,$(findstring TEP_USE_LIBDWARF, $(cpp)))
ldflags += -lbfd -ldwarf
endif

# rpath
ldflags += -Wl,-rpath='$$ORIGIN/../nrg/lib'

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

lib/pugixml: | $(lib_dir)
	@rm -rf $@
	# download the release and extract the archive
	cd $(lib_dir) && \
		wget https://github.com/zeux/pugixml/releases/download/v$(pugixml_ver)/pugixml-$(pugixml_ver).tar.gz && \
		tar xf pugixml-$(pugixml_ver).tar.gz --one-top-level=$(@F) --strip-components=1 && \
		rm -f pugixml-$(pugixml_ver).tar.gz
	# build
	installdir=$(shell pwd)/$@ && \
		cd $@ && mkdir -p build && cd build && \
		$(CMAKE) -DCMAKE_INSTALL_PREFIX=$$installdir -DCMAKE_INSTALL_LIBDIR=lib \
			-Wdev -Wdeprecated ..
	$(MAKE) -j $(nprocs) -C $@/build install

lib/json: | $(lib_dir)
	@rm -rf $@
	# download the release and extract the archive
	# no need to build since it is header-only
	cd $(lib_dir) && \
		wget https://github.com/nlohmann/json/releases/download/v$(json_ver)/include.zip && \
		unzip include.zip -d json && \
		rm -f include.zip

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
