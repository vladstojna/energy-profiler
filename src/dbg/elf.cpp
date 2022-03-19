#include "common.hpp"
#include "elf.hpp"
#include "error.hpp"
#include "params_structs.hpp"

#include <cassert>

namespace tep::dbg
{
    executable_header::executable_header(param x)
    {
        GElf_Ehdr hdr;
        if (!gelf_getehdr(x.elf.value, &hdr))
            throw exception(elf_errno(), elf_category());
        entrypoint_address = hdr.e_entry;
        switch (hdr.e_type)
        {
        case ET_DYN:
            type = executable_type::shared_object;
            break;
        case ET_EXEC:
            type = executable_type::executable;
            break;
        default:
            throw exception(errc::unsupported_object_type);
        }
    }

    function_symbol::function_symbol(param x) :
        name(elf_strptr(x.elf.value, x.header.sh_link, x.sym.st_name)),
        address(x.sym.st_value),
        size(x.sym.st_size),
        st_other(x.sym.st_other)
    {
        assert(GELF_ST_TYPE(x.sym.st_info) == STT_FUNC);
        switch (GELF_ST_VISIBILITY(x.sym.st_other))
        {
        case STV_DEFAULT:
            visibility = symbol_visibility::def;
            break;
        case STV_INTERNAL:
            visibility = symbol_visibility::internal;
            break;
        case STV_HIDDEN:
            visibility = symbol_visibility::hidden;
            break;
        case STV_PROTECTED:
            visibility = symbol_visibility::prot;
            break;
        default:
            throw exception(errc::invalid_symbol_visibility);
        }

        switch (GELF_ST_BIND(x.sym.st_info))
        {
        case STB_LOCAL:
            binding = symbol_binding::local;
            break;
        case STB_GLOBAL:
            binding = symbol_binding::global;
            break;
        case STB_WEAK:
            binding = symbol_binding::weak;
            break;
        default:
            throw exception(errc::unsupported_symbol_binding);
        }
    }
}