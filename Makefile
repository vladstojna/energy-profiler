cc := g++
std := c++17
override cpp +=

LIB_PREFIX ?= lib

# directories
src_dir := src
tgt_dir := bin
lib_dir := $(LIB_PREFIX)
obj_dir := obj
dep_dir := $(obj_dir)/.deps

# external libs
extlibs_incl := $(addprefix $(lib_dir)/, pugixml/include json/single_include expected/include)
extlibs_dirs := $(addprefix $(lib_dir)/, pugixml/lib)

# files
src  := $(shell find src/ -type f -name '*.cpp')
obj  := $(patsubst $(src_dir)/%.cpp, $(obj_dir)/%.o, $(src))
deps := $(patsubst $(src_dir)/%.cpp, $(dep_dir)/%.d, $(src))
tgt  := $(tgt_dir)/profiler

DEBUG ?=

system_clock ?=
ifdef system_clock
cflags += -DTEP_USE_SYSTEM_CLOCK
endif

cflags := -Wall -Wextra -Wno-unknown-pragmas -Wpedantic -fPIE -g -pthread
cflags += $(addprefix -I, $(extlibs_incl))
cflags += $(addprefix -I, include nrg/include)
cflags += -std=$(std)
cflags += $(addprefix -D, $(cpp))

ifdef DEBUG
cflags += -O0
else
cflags += -O3 -DNDEBUG -flto
ldflags += -flto
endif

# linker flags
ldflags := -pthread -lpugixml -lnrg -lstdc++fs -lelf -ldw
ldflags += $(addprefix -L, $(extlibs_dirs) nrg/lib)

ifneq (,$(findstring TEP_USE_LIBDWARF, $(cpp)))
ldflags += -lbfd -ldwarf
endif

# rpath
ldflags += -Wl,-rpath='$$ORIGIN/../nrg/lib'

# rules -----------------------------------------------------------------------

.PHONY: default
default: $(tgt)

$(tgt_dir):
	@mkdir -p $@
$(obj_dir) $(dep_dir):
	@mkdir -p $@/dbg

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
