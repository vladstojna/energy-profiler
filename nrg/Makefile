# directories
src_dir := src
tgt_dir := lib
obj_dir := obj
inc_dir := include
dep_dir := $(obj_dir)/.deps

incl := ../include $(inc_dir)

# files
src  := $(wildcard $(src_dir)/*.cpp)
obj  := $(patsubst $(src_dir)/%.cpp, $(obj_dir)/%.o, $(src))
deps := $(patsubst $(src_dir)/%.cpp, $(dep_dir)/%.d, $(src))
tgt  := $(tgt_dir)/libnrg

# debug build
dbg ?=
# GPU vendor or no-op
gpu ?=
# no-op CPU
cpu ?=

# autodetect GPU vendor
ifneq ($(shell command -v nvcc),)
gpu := GPU_NV
else
    ifneq ($(shell command -v hipcc),)
        gpu := GPU_AMD
    else
        gpu := GPU_NONE
    endif
endif

# linker flags
ldflags := -shared

# GPU vendor specific
ifeq ($(gpu),GPU_NV)

incl += /opt/cuda/include
ldflags += -lnvidia-ml

endif # $(gpu),GPU_NV

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

endif # $(gpu),GPU_AMD

# compiler flags
cc := g++
override cpp +=
cflags := -Wall -Wextra -Wno-unknown-pragmas -Wpedantic -fPIC -g
cflags += $(addprefix -I, $(incl))
cflags += -std=c++17
cflags += $(addprefix -D, $(cpp))

ifdef dbg
cflags += -O0
else
cflags += -O3 -DNDEBUG
endif

ifdef gpu
cflags += $(addprefix -D, $(gpu))
endif

ifdef cpu
cflags += $(addprefix -D, $(cpu))
endif

# archive
ar := ar
arflags := -crs

# rules -----------------------------------------------------------------------

.PHONY: default remake clean dynamic static

default: dynamic

$(tgt_dir):
	@mkdir -p $@
$(obj_dir):
	@mkdir -p $@
$(dep_dir):
	@mkdir -p $@

dynamic: $(tgt).so

static: $(tgt).a

$(tgt).so: $(obj) | $(tgt_dir)
	$(cc) $^ $(ldflags) -o $@

$(tgt).a: $(obj) | $(tgt_dir)
	$(ar) $(arflags) $@ $^

$(obj_dir)/%.o: $(src_dir)/%.cpp $(dep_dir)/%.d | $(obj_dir) $(dep_dir)
	$(cc) -MT $@ -MMD -MP -MF $(dep_dir)/$*.d $(cflags) -c -o $@ $<

$(deps):

include $(wildcard $(deps))

remake: clean default

clean:
	rm -rf $(tgt_dir) $(obj_dir)
