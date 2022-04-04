#pragma once

#include "elf.hpp"
#include "dwarf.hpp"
#include "common.hpp"

#include <gelf.h>

namespace tep::dbg
{
    struct executable_header::param
    {
        elf_descriptor& elf;
    };

    struct function_symbol::param
    {
        const elf_descriptor& elf;
        const GElf_Shdr& header;
        const GElf_Sym& sym;
    };

    struct function_addresses::param
    {
        Dwarf_Die& func_die;
    };

    struct source_line::param
    {
        Dwarf_Line* line;
    };

    struct source_location::call_param : function_addresses::param
    {
        Dwarf_Files* files;
    };

    struct source_location::decl_param : function_addresses::param {};
    struct inline_instance::param : source_location::call_param {};
    struct inline_instances::param : inline_instance::param {};
    struct function::param : source_location::decl_param {};

    struct compilation_unit::param
    {
        Dwarf_Die& cu_die;
    };

} // namespace tep::dbg
