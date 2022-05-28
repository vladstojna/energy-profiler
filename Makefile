cc := g++
std := c++17
override cpp +=

# directories
src_dir := src
tgt_dir := bin
lib_dir := lib
obj_dir := obj
dep_dir := $(obj_dir)/.deps

ELFUTILS_PREFIX ?=
PUGIXML_PREFIX  ?= $(lib_dir)/pugixml
JSON_PREFIX     ?= $(lib_dir)/json
EXPECTED_PREFIX ?= $(lib_dir)/expected

DEBUG ?=
system_clock ?=

# external libs
extlibs_incl := $(PUGIXML_PREFIX)/include $(JSON_PREFIX)/single_include $(EXPECTED_PREFIX)/include
extlibs_dirs := $(PUGIXML_PREFIX)/lib

ifdef ELFUTILS_PREFIX
extlibs_incl += $(ELFUTILS_PREFIX)/include
extlibs_dirs += $(ELFUTILS_PREFIX)/lib
endif

# files
src  := $(shell find src/ -type f -name '*.cpp')
obj  := $(patsubst $(src_dir)/%.cpp, $(obj_dir)/%.o, $(src))
deps := $(patsubst $(src_dir)/%.cpp, $(dep_dir)/%.d, $(src))
tgt  := $(tgt_dir)/profiler

cflags := -Wall -Wextra -Wno-unknown-pragmas -Wpedantic -fPIE -g -pthread
cflags += $(addprefix -I, $(extlibs_incl))
cflags += $(addprefix -I, include nrg/include)
cflags += -std=$(std)
cflags += $(addprefix -D, $(cpp))

ifdef system_clock
cflags += -DTEP_USE_SYSTEM_CLOCK
endif

# linker flags
ldflags := -pthread -lpugixml -lnrg -lstdc++fs -lelf -ldw
ldflags += $(addprefix -L, $(extlibs_dirs) nrg/lib)

# rpath
ldflags += -Wl,-rpath='$$ORIGIN/../nrg/lib'

ifdef DEBUG
cflags += -O0
else
cflags += -O3 -DNDEBUG -flto
ldflags += -flto
endif

# rules -----------------------------------------------------------------------

.PHONY: default
default: $(tgt)

$(tgt_dir):
	@mkdir -p $@
$(obj_dir) $(dep_dir):
	@mkdir -p $@/dbg $@/output

$(tgt): $(obj) | $(tgt_dir)
	$(cc) $^ $(ldflags) -o $@

$(obj_dir)/%.o: $(src_dir)/%.cpp $(dep_dir)/%.d | $(obj_dir) $(dep_dir)
	$(cc) -MT $@ -MMD -MP -MF $(dep_dir)/$*.d $(cflags) -c -o $@ $<

$(deps):

include $(wildcard $(deps))

.PHONY: remake
remake: clean
	$(MAKE) default

# do not clean libraries because
# those may take a while to rebuild
.PHONY: clean
clean:
	rm -rf $(tgt_dir) $(obj_dir) $(dep_dir)
