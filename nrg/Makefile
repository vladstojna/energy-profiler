# directories
src_dir := src
tgt_dir := lib
obj_dir := obj
inc_dir := include
dep_dir := $(obj_dir)/.deps
lib_dir := ../lib

libs_incl := $(addprefix $(lib_dir)/, expected/include)
incl := ../include $(inc_dir) $(libs_incl)

override cpp +=

# debug build
dbg ?=
# GPU vendor or no-op
gpu ?=
# no-op CPU
cpu ?=

src := $(wildcard $(src_dir)/*.cpp)
# include architecture specific source files
ifeq (CPU_NONE,$(cpu)) # force no-op CPU reader
	src += $(wildcard $(src_dir)/none/reader_cpu.cpp)
else ifneq (,$(findstring NRG_X86_64, $(cpp))) # force x86_64
	cpu_arch := x86_64
else ifneq (,$(findstring NRG_PPC64, $(cpp))) # force ppc64
	cpu_arch := ppc64
else ifeq (x86_64,$(shell uname -m))
	cpu_arch := x86_64
else ifneq (,$(findstring ppc64,$(shell uname -m)))
	cpu_arch := ppc64
endif # (CPU_NONE,$(cpu))
ifneq (CPU_NONE,$(cpu))
	src += $(wildcard $(src_dir)/common/cpu/*.cpp)
	src += $(wildcard $(src_dir)/$(cpu_arch)/*.cpp)
endif # (CPU_NONE,$(cpu))

# include GPU vendor specific source files
ifeq (GPU_NONE,$(gpu)) # force no-op GPU reader
# empty
else ifeq (GPU_NV,$(gpu)) # force NVIDIA
	gpu_vendor := nvidia
else ifeq (GPU_AMD,$(gpu)) # force AMD
	gpu_vendor := amd
else ifneq ($(shell command -v nvcc;),)
	gpu := GPU_NV
	gpu_vendor := nvidia
else ifneq ($(shell command -v hipcc;),)
	gpu := GPU_AMD
	gpu_vendor := amd
else # no GPU
	gpu := GPU_NONE
endif # (GPU_NONE,$(gpu))
ifneq (GPU_NONE,$(gpu))
	src += $(wildcard $(src_dir)/common/gpu/*.cpp)
	src += $(wildcard $(src_dir)/$(gpu_vendor)/*.cpp)
else # ifeq (GPU_NONE,$(gpu))
	src += $(wildcard $(src_dir)/none/reader_gpu.cpp)
endif # (GPU_NONE,$(cpu))

obj  := $(patsubst $(src_dir)/%.cpp, $(obj_dir)/%.o, $(src))
deps := $(patsubst $(src_dir)/%.cpp, $(dep_dir)/%.d, $(src))
tgt  := $(tgt_dir)/libnrg

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
cflags := -Wall -Wextra -Wno-unknown-pragmas -Wpedantic -fPIC -g
cflags += $(addprefix -I, $(incl))
cflags += -std=c++17
cflags += $(addprefix -D, $(cpp))

ifdef dbg
cflags += -O0
else
cflags += -O3 -DNDEBUG -flto
ldflags += -flto
endif

ifdef gpu
cflags += $(addprefix -D, $(gpu))
endif

ifdef cpu
cflags += $(addprefix -D, $(cpu))
endif

system_clock ?=
ifdef system_clock
cflags += -DNRG_USE_SYSTEM_CLOCK
endif

# archive
ar := ar
arflags := -crs

# rules -----------------------------------------------------------------------

.PHONY: default remake clean dynamic static

default: dynamic

$(tgt_dir):
	@mkdir -p $@

dynamic: $(tgt).so

static: $(tgt).a

$(tgt).so: $(obj) | $(tgt_dir)
	$(cc) $^ $(ldflags) -o $@

$(tgt).a: $(obj) | $(tgt_dir)
	$(ar) $(arflags) $@ $^

$(obj_dir)/%.o: $(src_dir)/%.cpp $(dep_dir)/%.d
	@mkdir -p $(dir $@)
	@mkdir -p $(dir $(dep_dir)/$*.d)
	$(cc) -MT $@ -MMD -MP -MF $(dep_dir)/$*.d $(cflags) -c -o $@ $<

$(deps):

include $(wildcard $(deps))

remake: clean default

clean:
	rm -rf $(tgt_dir) $(obj_dir)
