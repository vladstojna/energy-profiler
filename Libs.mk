pugixml_ver := 1.11.4
json_ver := 3.10.5
expected_ver := 0.5.0

libdir := lib

dir_targets  := $(addprefix $(libdir)/, pugixml json expected)
pugixml_build_files := $(addprefix $(libdir)/, pugixml/lib pugixml/include)

CMAKE := $(shell command -v cmake;)

.PHONY: default
default: build

.PHONY: clone
clone: $(dir_targets)

.PHONY: build
build: clone $(pugixml_build_files)

.PHONY: clean
clean:
	rm -rf $(libdir)

.INTERMEDIATE: build_pugixml_procedure
build_pugixml_procedure: $(libdir)/pugixml
	installdir=$(shell pwd)/$< && \
		cd $< && mkdir -p build && cd build && \
		$(CMAKE) -DCMAKE_INSTALL_PREFIX=$$installdir -DCMAKE_INSTALL_LIBDIR=lib \
			-Wdev -Wdeprecated ..
	$(MAKE) -C $</build install

$(libdir):
	@mkdir -p $@

$(libdir)/pugixml: | $(libdir)
	cd $(libdir) && \
		wget https://github.com/zeux/pugixml/releases/download/v$(pugixml_ver)/pugixml-$(pugixml_ver).tar.gz && \
		tar xf pugixml-$(pugixml_ver).tar.gz --one-top-level=$(@F) --strip-components=1 && \
		rm -f pugixml-$(pugixml_ver).tar.gz

$(pugixml_build_files): build_pugixml_procedure

$(libdir)/json: | $(libdir)
	cd $(libdir) && \
		wget https://github.com/nlohmann/json/releases/download/v$(json_ver)/include.zip && \
		unzip -q include.zip -d json && \
		rm -f include.zip

$(libdir)/expected: | $(libdir)
	@mkdir -p $(libdir)/expected/include/nonstd
	wget -P $(libdir)/expected/include/nonstd \
		https://github.com/martinmoene/expected-lite/releases/download/v$(expected_ver)/expected.hpp
