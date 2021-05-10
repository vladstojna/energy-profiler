# directories
src_dir  := src
tgt_dir  := lib
obj_dir  := obj
incl_dir := include
dep_dir  := $(obj_dir)/.deps

incl := ../util

# files
src  := $(wildcard $(src_dir)/*.cpp)
obj  := $(patsubst $(src_dir)/%.cpp, $(obj_dir)/%.o, $(src))
deps := $(patsubst $(src_dir)/%.cpp, $(dep_dir)/%.d, $(src))
tgt  := $(tgt_dir)/libnrg.so

DEBUG ?=
gpu ?=

# linker flags
ldflags := -shared

# GPU vendor specific
ifeq ($(gpu),GPU_NV)

incl += /opt/cuda/include
ldflags += -lnvidia-ml

endif

ifeq ($(gpu),GPU_AMD)

rocm_ver ?=
ifdef rocm_ver
rocm := rocm-$(rocm_ver)
else
rocm := rocm
endif

rsmi_base := /opt/$(rocm)/rocm_smi
export LD_RUN_PATH=$(rsmi_base)/lib

incl += $(rsmi_base)/include
ldflags += -lrocm_smi64
ldflags += $(addprefix -L, $(LD_RUN_PATH))

endif

# compiler flags
cc := g++
cflags := -Wall -Wextra -Wno-unknown-pragmas -fPIC -g
cflags += $(addprefix -I, $(incl))
cflags += -std=c++17

ifdef DEBUG
cflags += -O0
else
cflags += -O3 -DNDEBUG
endif

ifdef gpu
cflags += $(addprefix -D, $(gpu))
endif


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
