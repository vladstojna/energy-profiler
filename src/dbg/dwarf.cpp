#include "common.hpp"
#include "dwarf.hpp"
#include "error.hpp"
#include "params_structs.hpp"

#include <dwarf.h>

#include <algorithm>
#include <cassert>

namespace
{
    bool operator<(
        const tep::dbg::source_location& lhs,
        const tep::dbg::source_location& rhs)
        noexcept
    {
        if (lhs.file < rhs.file)
            return true;
        if (lhs.file == rhs.file)
        {
            if (lhs.line_number < rhs.line_number)
                return true;
            if (lhs.line_number == rhs.line_number)
                return lhs.line_column < rhs.line_column;
        }
        return false;
    }

    std::filesystem::path build_path(Dwarf_Die& cu_die)
    {
        namespace fs = std::filesystem;
        Dwarf_Attribute attr;
        fs::path cu_path =
            dwarf_formstring(dwarf_attr(&cu_die, DW_AT_comp_dir, &attr));
        cu_path /= dwarf_formstring(dwarf_attr(&cu_die, DW_AT_name, &attr));
        return cu_path;
    }

    std::vector<Dwarf_Die> get_funcs(Dwarf_Die& cu_die)
    {
        using tep::dbg::exception;
        using tep::dbg::dwarf_category;
        std::vector<Dwarf_Die> funcs;
        ptrdiff_t res = dwarf_getfuncs(&cu_die, [](Dwarf_Die* func_die, void* data)
            {
                auto& funcs = *reinterpret_cast<std::vector<Dwarf_Die>*>(data);
                funcs.push_back(*func_die);
                return static_cast<int>(DWARF_CB_OK);
            }, &funcs, 0);
        if (res != 0)
            throw exception(dwarf_errno(), dwarf_category());
        return funcs;
    }

    size_t get_inline_instance_count(Dwarf_Die& func_die)
    {
        using tep::dbg::exception;
        using tep::dbg::dwarf_category;
        size_t count = 0;
        int res = dwarf_func_inline_instances(&func_die, [](Dwarf_Die*, void* data)
            {
                auto& count = *reinterpret_cast<size_t*>(data);
                count += 1;
                return static_cast<int>(DWARF_CB_OK);
            }, &count);
        if (res != 0)
            throw exception(dwarf_errno(), dwarf_category());
        return count;
    }

    std::vector<Dwarf_Die> get_inline_instances(Dwarf_Die& func_die)
    {
        using tep::dbg::exception;
        using tep::dbg::dwarf_category;
        std::vector<Dwarf_Die> insts;
        int res = dwarf_func_inline_instances(&func_die, [](Dwarf_Die* instance, void* data)
            {
                auto& insts = *reinterpret_cast<std::vector<Dwarf_Die>*>(data);
                insts.push_back(*instance);
                return static_cast<int>(DWARF_CB_OK);
            }, &insts);
        if (res != 0)
            throw exception(dwarf_errno(), dwarf_category());
        return insts;
    }

    std::pair<Dwarf_Files*, size_t> get_source_files(Dwarf_Die& cu_die)
    {
        using tep::dbg::exception;
        using tep::dbg::dwarf_category;
        Dwarf_Files* files;
        size_t nfiles;
        if (0 != dwarf_getsrcfiles(&cu_die, &files, &nfiles))
            throw exception(dwarf_errno(), dwarf_category());
        return { files, nfiles };
    }

    std::vector<tep::dbg::contiguous_range> get_ranges(Dwarf_Die& die)
    {
        using tep::dbg::exception;
        using tep::dbg::contiguous_range;
        using tep::dbg::dwarf_category;
        std::vector<contiguous_range> rngs;
        Dwarf_Addr base;
        contiguous_range rng;
        ptrdiff_t offset = 0;
        while ((offset = dwarf_ranges(&die, offset, &base, &rng.low_pc, &rng.high_pc)))
        {
            if (offset == -1)
                throw exception(dwarf_errno(), dwarf_category());
            rngs.push_back(rng);
        }
        return rngs;
    };
}

namespace tep::dbg
{
    source_line::source_line(const param& x)
    {
        auto line = x.line;
        if (const char* str = dwarf_linesrc(line, nullptr, nullptr))
            file = str;
        else
            throw exception(dwarf_errno(), dwarf_category());
        if (dwarf_lineaddr(line, &address))
            throw exception(dwarf_errno(), dwarf_category());
        if (int val; dwarf_lineno(line, &val))
            throw exception(dwarf_errno(), dwarf_category());
        else if (val < 0)
            throw exception(errc::line_number_overflow);
        else
            number = val;
        if (int val; dwarf_linecol(line, &val))
            throw exception(dwarf_errno(), dwarf_category());
        else if (val < 0)
            throw exception(errc::line_column_overflow);
        else
            column = val;
        if (dwarf_linebeginstatement(line, &new_statement))
            throw exception(dwarf_errno(), dwarf_category());
        if (dwarf_lineendsequence(line, &end_text_sequence))
            throw exception(dwarf_errno(), dwarf_category());
        if (dwarf_lineblock(line, &new_basic_block))
            throw exception(dwarf_errno(), dwarf_category());
        if (bool pe; dwarf_lineprologueend(line, &pe))
            throw exception(dwarf_errno(), dwarf_category());
        else if (pe)
            ctx = line_context::prologue_end;
        else if (bool eb; dwarf_lineepiloguebegin(line, &eb))
            throw exception(dwarf_errno(), dwarf_category());
        else if (eb)
            ctx = line_context::epilogue_begin;
        else
            ctx = line_context::none;
    }

    source_location::source_location(decl_param x)
    {
        auto& func_die = x.func_die;
        if (const char* str = dwarf_decl_file(&func_die); str)
            file = str;
        if (int val; 0 == dwarf_decl_line(&func_die, &val))
            line_number = static_cast<uint32_t>(val);
        if (int val; 0 == dwarf_decl_column(&func_die, &val))
            line_column = static_cast<uint32_t>(val);
    }

    source_location::source_location(call_param x)
    {
        auto& inst = x.func_die;
        auto files = x.files;
        Dwarf_Attribute attr;
        Dwarf_Word val;
        if (dwarf_hasattr_integrate(&inst, DW_AT_call_file))
        {
            if (0 != dwarf_formudata(dwarf_attr_integrate(&inst, DW_AT_call_file, &attr), &val))
                throw exception(dwarf_errno(), dwarf_category());
            file = dwarf_filesrc(files, val, nullptr, nullptr);
        }
        if (dwarf_hasattr_integrate(&inst, DW_AT_call_line))
        {
            if (0 != dwarf_formudata(dwarf_attr_integrate(&inst, DW_AT_call_line, &attr), &val))
                throw exception(dwarf_errno(), dwarf_category());
            if (val >= std::numeric_limits<uint32_t>::max())
                throw exception(errc::line_number_overflow);
            else
                line_number = val;
        }
        if (dwarf_hasattr_integrate(&inst, DW_AT_call_column))
        {
            if (0 != dwarf_formudata(dwarf_attr_integrate(&inst, DW_AT_call_column, &attr), &val))
                throw exception(dwarf_errno(), dwarf_category());
            if (val >= std::numeric_limits<uint32_t>::max())
                throw exception(errc::line_column_overflow);
            else
                line_column = val;
        }
    }

    inline_instance::inline_instance(const param& x) :
        call_loc(std::in_place, x),
        addresses(x)
    {
        auto& inst = x.func_die;
        assert(dwarf_tag(&inst) == DW_TAG_inlined_subroutine);
        dwarf_entrypc(&inst, &entry_pc);
        if (call_loc->file.empty() || !call_loc->line_number)
            call_loc = std::nullopt;
    }

    compilation_unit::compilation_unit(const param& x) :
        path(build_path(x.cu_die)),
        addresses(get_ranges(x.cu_die))
    {
        std::sort(addresses.begin(), addresses.end(),
            [](const contiguous_range& lhs, const contiguous_range& rhs)
            {
                if (lhs.low_pc < rhs.low_pc)
                    return true;
                if (lhs.low_pc == rhs.low_pc)
                    return lhs.high_pc < rhs.high_pc;
                return false;
            });

        load_lines(x);
        load_functions(x);
    }

    void compilation_unit::load_lines(const param& x)
    {
        Dwarf_Lines* dlines;
        size_t nlines;
        if (0 != dwarf_getsrclines(&x.cu_die, &dlines, &nlines))
            throw exception(dwarf_errno(), dwarf_category());
        for (size_t l = 0; l < nlines; ++l)
        {
            if (Dwarf_Line* line = dwarf_onesrcline(dlines, l); !line)
                throw exception(dwarf_errno(), dwarf_category());
            else
                lines.emplace_back(source_line::param{ line });
        }
        std::sort(lines.begin(), lines.end(), [](const source_line& lhs, const source_line& rhs)
            {
                if (lhs.file < rhs.file)
                    return true;
                if (lhs.file == rhs.file)
                {
                    if (lhs.number < rhs.number)
                        return true;
                    if (lhs.number == rhs.number)
                    {
                        if (lhs.column < rhs.column)
                            return true;
                        if (lhs.column == rhs.column)
                            return lhs.address < rhs.address;
                    }
                }
                return false;
            });
    }

    void compilation_unit::load_functions(const param& x)
    {
        static auto pred = [](const function& lhs, const function& rhs)
        {
            return lhs.die_name == rhs.die_name &&
                lhs.decl_loc == rhs.decl_loc &&
                lhs.linkage_name == rhs.linkage_name;
        };

        auto [files, nfiles] = get_source_files(x.cu_die);
        // initially, add all concrete functions
        container<function> inlined;
        passkey<compilation_unit> key;
        for (auto& func_die : get_funcs(x.cu_die))
        {
            bool is_inline =
                dwarf_func_inline(&func_die);
            bool is_concrete =
                dwarf_hasattr(&func_die, DW_AT_low_pc) ||
                dwarf_hasattr(&func_die, DW_AT_ranges);
            if (is_inline && get_inline_instance_count(func_die))
            {
                // functions with inlined instances are added to a different
                // vector to be processed later
                assert(!is_concrete);
                inlined.emplace_back(function::param{ func_die })
                    .set_inline_instances(inline_instances{ {func_die, files} }, key);
            }
            if (is_concrete)
            {
                funcs.emplace_back(function::param{ func_die })
                    .set_out_of_line_addresses(function_addresses{ {func_die} }, key);
            }
        }

        auto separator = std::partition(inlined.begin(), inlined.end(),
            [this](const function& inl)
            {
                return funcs.end() == std::find_if(
                    funcs.begin(), funcs.end(), [&](const function& x)
                    {
                        return pred(x, inl);
                    });
            });

        funcs.insert(funcs.end(), inlined.begin(), separator);
        for (auto& f : funcs)
        {
            for (auto it = separator; it != inlined.end(); ++it)
                if (pred(f, *it))
                    f.set_inline_instances(*std::move(*it).instances, key);
        }

        std::sort(funcs.begin(), funcs.end(),
            [](const function& lhs, const function& rhs)
            {
                if (!lhs.decl_loc && !rhs.decl_loc)
                    return lhs.die_name < rhs.die_name;
                if (lhs.decl_loc && !rhs.decl_loc)
                    return true;
                if (!lhs.decl_loc && rhs.decl_loc)
                    return false;
                return *lhs.decl_loc < *rhs.decl_loc;
            });
    }

    inline_instances::inline_instances(const param& x) :
        insts(get_instances(x))
    {}

    std::vector<inline_instance>
        inline_instances::get_instances(const param& x)
    {
        std::vector<inline_instance> retval;
        assert(dwarf_func_inline(&x.func_die));
        auto inst_dies = get_inline_instances(x.func_die);
        assert(inst_dies.size() > 0);
        retval.reserve(inst_dies.size());
        for (auto& die : inst_dies)
        {
            assert(dwarf_tag(&die) == DW_TAG_inlined_subroutine);
            retval.emplace_back(inline_instance::param{ die, x.files });
        }
        return retval;
    }

    function_addresses::function_addresses(const param& x) :
        values(get_ranges(x.func_die))
    {
        assert(
            dwarf_hasattr(&x.func_die, DW_AT_low_pc) ||
            dwarf_hasattr(&x.func_die, DW_AT_ranges)
        );
    }

    function::function(const param& x) :
        die_name(dwarf_diename(&x.func_die)),
        decl_loc(std::in_place, source_location::decl_param{ x.func_die })
    {
        assert(dwarf_tag(&x.func_die) == DW_TAG_subprogram);
        if (decl_loc->file.empty() || !decl_loc->line_number)
            decl_loc = std::nullopt;

        if (dwarf_hasattr_integrate(&x.func_die, DW_AT_external))
        {
            if (dwarf_hasattr_integrate(&x.func_die, DW_AT_linkage_name))
            {
                Dwarf_Attribute attr;
                linkage_name = dwarf_formstring(
                    dwarf_attr_integrate(&x.func_die, DW_AT_linkage_name, &attr));
            }
            else
                linkage_name = die_name;
        }
    }

    void function::set_out_of_line_addresses(
        function_addresses x, passkey<compilation_unit>)
    {
        addresses = std::move(x);
    }

    void function::set_inline_instances(
        inline_instances x, passkey<compilation_unit>)
    {
        instances = std::move(x);
    }

    bool function::is_static() const noexcept
    {
        return !is_extern();
    }

    bool function::is_extern() const noexcept
    {
        return bool(linkage_name);
    }

    bool operator==(
        const source_location& lhs, const source_location& rhs) noexcept
    {
        return lhs.file == rhs.file &&
            lhs.line_number == rhs.line_number &&
            lhs.line_column == rhs.line_column;
    }
} // namespace tep::dbg
