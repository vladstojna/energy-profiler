# get number of processors
ifeq ($(shell uname -s),Linux)
nprocs := $(shell grep -c ^processor /proc/cpuinfo)
else
nprocs := 1
endif

# directories
src_dir  := src
tgt_dir  := lib
obj_dir  := obj
incl_dir := include
dep_dir  := $(obj_dir)/.deps

incl   := /opt/cuda/include ../lib/papi/include ../util
libs   := /usr/lib64 ../lib/papi/lib
libs_L := $(addprefix -L, $(libs))

#files
src  := $(wildcard $(src_dir)/*.cpp)
obj  := $(patsubst $(src_dir)/%.cpp, $(obj_dir)/%.o, $(src))
deps := $(patsubst $(src_dir)/%.cpp, $(dep_dir)/%.d, $(src))
tgt  := $(tgt_dir)/libnrg.so

# compiler flags
cc := g++
DEBUG ?=
cflags := -Wall -Wextra -Wno-unknown-pragmas -fPIC -g
ifeq ($(DEBUG),true)
cflags += -O0
else
cflags += -O3 -DNDEBUG
endif
cflags += $(addprefix -I, $(incl))
cflags += -std=c++17

# linker flags
ldflags := -shared

# rules -----------------------------------------------------------------------

default: $(tgt) $(incl_dir)

$(tgt_dir):
	@mkdir -p $@
$(obj_dir):
	@mkdir -p $@
$(dep_dir):
	@mkdir -p $@

$(tgt): $(obj) | $(tgt_dir)
	$(cc) $^ $(ldflags) -o $@

$(incl_dir):
	ln -s $(src_dir) $@

$(obj_dir)/%.o: $(src_dir)/%.cpp $(dep_dir)/%.d | $(obj_dir) $(dep_dir)
	$(cc) -MT $@ -MMD -MP -MF $(dep_dir)/$*.d $(cflags) -c -o $@ $<

$(deps):

include $(wildcard $(deps))

clean:
	rm -rf $(tgt_dir) $(obj_dir) $(incl_dir)
