#include "common.hpp"
#include "dwarf.hpp"
#include "error.hpp"
#include "params_structs.hpp"

#include <dwarf.h>

#include <algorithm>
#include <cassert>
#include <iostream>

namespace
{
    const tep::dbg::function_base& to_function_base(
        const tep::dbg::any_function& any) noexcept
    {
        return std::holds_alternative<tep::dbg::normal_function>(any) ?
            std::get<tep::dbg::normal_function>(any) :
            std::get<tep::dbg::static_function>(any);
    }

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

    tep::dbg::ranges get_ranges(Dwarf_Die& die)
    {
        using tep::dbg::ranges;
        using tep::dbg::exception;
        using tep::dbg::contiguous_range;
        using tep::dbg::dwarf_category;
        ranges rngs;
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

    tep::dbg::static_function::data_t create_function_data(
        Dwarf_Die& func_die, Dwarf_Files* files)
    {
        using tep::dbg::inline_instance;
        using tep::dbg::inline_instances;
        using tep::dbg::function_addresses;
        if (dwarf_func_inline(&func_die))
        {
            inline_instances instances;
            auto instance_dies = get_inline_instances(func_die);
            for (auto& inst : instance_dies)
                instances.emplace_back(inline_instance::param{ inst, files });
            return instances;
        }
        else if (auto ranges = get_ranges(func_die); ranges.size() > 1)
            return ranges;
        return function_addresses{ {func_die} };
    }
}

namespace tep::dbg
{
    function_addresses::function_addresses(const param& x)
    {
        auto& func_die = x.func_die;
        if (Dwarf_Addr addr; 0 == dwarf_entrypc(&func_die, &addr))
            entry_pc = addr;
        if (0 != dwarf_lowpc(&func_die, &crange.low_pc))
            throw exception(errc::no_low_pc_concrete);
        if (0 != dwarf_highpc(&func_die, &crange.high_pc))
            throw exception(errc::no_high_pc_concrete);
    }

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
        call_loc(std::in_place, x)
    {
        auto get_ranges = [](Dwarf_Die& die)
        {
            std::vector<contiguous_range> ranges;
            Dwarf_Addr base;
            contiguous_range rng;
            ptrdiff_t offset = 0;
            while ((offset = dwarf_ranges(&die, offset, &base, &rng.low_pc, &rng.high_pc)))
            {
                if (offset == -1)
                    throw exception(dwarf_errno(), dwarf_category());
                ranges.push_back(rng);
            }
            return ranges;
        };

        auto& inst = x.func_die;
        assert(dwarf_tag(&inst) == DW_TAG_inlined_subroutine);
        if (Dwarf_Addr addr; 0 == dwarf_entrypc(&inst, &addr))
            entry_pc = addr;
        auto ranges = get_ranges(inst);
        if (ranges.size() == 1)
            addresses = ranges.front();
        else if (ranges.size() > 1)
            addresses = ranges;
        else
        {
            contiguous_range rng;
            if (0 != dwarf_lowpc(&inst, &rng.low_pc))
                throw exception(errc::no_low_pc_inlined);
            if (0 != dwarf_highpc(&inst, &rng.high_pc))
                throw exception(errc::no_high_pc_inlined);
            addresses = rng;
        }
    }

    function_base::function_base(const param& x) :
        die_name(dwarf_diename(&x.func_die)),
        decl_loc(std::in_place, source_location::decl_param{ x.func_die })
    {
        if (decl_loc->file.empty() || !decl_loc->line_number)
            decl_loc = std::nullopt;
    }

    static_function::static_function(const param& x) :
        function_base({ x.func_die }),
        data(create_function_data(x.func_die, x.files))
    {}

    normal_function::normal_function(const param& x) :
        static_function(x)
    {
        auto& func_die = x.func_die;
        assert(dwarf_hasattr_integrate(&func_die, DW_AT_linkage_name));
        if (!dwarf_hasattr_integrate(&func_die, DW_AT_linkage_name))
            throw exception(errc::no_linkage_name);
        Dwarf_Attribute attr;
        linkage_name = dwarf_formstring(
            dwarf_attr_integrate(&func_die, DW_AT_linkage_name, &attr));
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
        auto& cu_die = x.cu_die;
        auto [files, nfiles] = get_source_files(cu_die);
        auto func_dies = get_funcs(cu_die);
        for (auto& func_die : func_dies)
        {
            bool is_inline = dwarf_func_inline(&func_die);
            bool is_static =
                !dwarf_hasattr_integrate(&func_die, DW_AT_linkage_name) ||
                !dwarf_hasattr_integrate(&func_die, DW_AT_external);
            bool is_concrete =
                is_inline ||
                dwarf_hasattr_integrate(&func_die, DW_AT_low_pc) ||
                dwarf_hasattr_integrate(&func_die, DW_AT_ranges);

            if (is_static && is_concrete)
            {
                if (is_inline && get_inline_instance_count(func_die) == 0)
                    continue;
                funcs.emplace_back(
                    std::in_place_type<static_function>,
                    static_function::param{ func_die, files });
            }
            else if (is_concrete)
            {
                if (is_inline && get_inline_instance_count(func_die) == 0)
                    continue;
                funcs.emplace_back(
                    std::in_place_type<normal_function>,
                    normal_function::param{ func_die, files });
            }
        }
        std::sort(funcs.begin(), funcs.end(),
            [](const any_function& lhs, const any_function& rhs)
            {
                const auto& lhs_base = to_function_base(lhs);
                const auto& rhs_base = to_function_base(rhs);
                if (!lhs_base.decl_loc && !rhs_base.decl_loc)
                    return lhs_base.die_name < rhs_base.die_name;
                if (lhs_base.decl_loc && !rhs_base.decl_loc)
                    return true;
                if (!lhs_base.decl_loc && rhs_base.decl_loc)
                    return false;
                return *lhs_base.decl_loc < *rhs_base.decl_loc;
            });
    }
} // namespace tep::dbg
