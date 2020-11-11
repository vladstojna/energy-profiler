cc=g++

ifdef NDEBUG
cflags=-Wall -Wextra -O3 -D NDEBUG
else
cflags=-Wall -Wextra -g -O0
endif
cppflags=-std=c++17
libs=-pthread -lbfd -ldwarf

# directories
tgt_dir=bin
obj_dir=obj
src_dir=src
dep_dir=.deps

# files
src=$(wildcard src/*.cpp)
obj=$(patsubst $(src_dir)/%.cpp,$(obj_dir)/%.o,$(src))
deps=$(patsubst $(src_dir)/%.cpp,$(dep_dir)/%.d,$(src))
tgt=$(tgt_dir)/profiler

# rules -----------------------------------------------------------------------

default: all

.PHONY: all
all: directories $(tgt)

.PHONY: directories
directories: | $(tgt_dir) $(obj_dir) $(dep_dir)

$(tgt_dir):
	@mkdir -p $@
$(obj_dir):
	@mkdir -p $@
$(dep_dir):
	@mkdir -p $@

$(tgt): $(obj)
	$(cc) $^ $(libs) -o $@

obj/%.o: src/%.cpp $(dep_dir)/%.d
	$(cc) -MT $@ -MMD -MP -MF $(dep_dir)/$*.d $(cflags) $(cppflags) -c -o $@ $<

$(deps):

include $(wildcard $(deps))

.PHONY: remake
remake: clean all

.PHONY: clean
clean:
	rm -rf $(tgt_dir) $(obj_dir) $(dep_dir)
