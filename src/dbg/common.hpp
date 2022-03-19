#pragma once

#include <string_view>

#include <libelf.h>
#include <elfutils/libdw.h>

namespace tep::dbg
{
    struct ro_file_descriptor
    {
        int value;
        explicit ro_file_descriptor(std::string_view);
        ~ro_file_descriptor();
    };

    struct elf_descriptor
    {
        Elf* value;
        explicit elf_descriptor(ro_file_descriptor&);
        ~elf_descriptor();
    };

    struct dwarf_descriptor
    {
        Dwarf* value;
        explicit dwarf_descriptor(elf_descriptor&);
        ~dwarf_descriptor();
    };

} // namespace tep::dbg

