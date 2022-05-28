# directories
src_dir := src
tgt_dir := lib
obj_dir := obj
inc_dir := include
dep_dir := $(obj_dir)/.deps
lib_dir := ../lib
override cpp +=

EXPECTED_PREFIX ?= $(lib_dir)/expected
incl := ../include $(inc_dir) $(EXPECTED_PREFIX)/include

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
# CUDA installation prefix
CUDA_ROOT ?= /opt/cuda
incl      += $(CUDA_ROOT)/include
ldflags   += -L$(CUDA_ROOT)/lib$(shell getconf LONG_BIT;)
ldflags   += -lnvidia-ml
endif # $(gpu),GPU_NV

ifeq ($(gpu),GPU_AMD)
ROCM_ROOT ?= /opt/rocm
incl      += $(ROCM_ROOT)/rocm_smi/include
ldflags   += -L$(ROCM_ROOT)/rocm_smi/lib
ldflags   += -lrocm_smi64
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

# archive
ar := ar
arflags := -crs

# rules -----------------------------------------------------------------------

.PHONY: default
default: dynamic

$(tgt_dir):
	@mkdir -p $@

.PHONY: dynamic
dynamic: $(tgt).so

.PHONY: static
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

.PHONY: remake
remake: clean
	$(MAKE) default

.PHONY: clean
clean:
	rm -rf $(tgt_dir) $(obj_dir)
