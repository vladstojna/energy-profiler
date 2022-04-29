#include "object_info.hpp"
#include "common.hpp"
#include "error.hpp"
#include "params_structs.hpp"

#include <algorithm>

namespace {
std::pair<GElf_Shdr, Elf_Scn *>
find_symtab(const tep::dbg::elf_descriptor &elf) {
  using tep::dbg::elf_category;
  using tep::dbg::errc;
  using tep::dbg::exception;
  std::pair<GElf_Shdr, Elf_Scn *> retval;
  for (retval.second = elf_nextscn(elf.value, nullptr); retval.second;
       retval.second = elf_nextscn(elf.value, retval.second)) {
    if (!gelf_getshdr(retval.second, &retval.first))
      throw exception(elf_errno(), elf_category());
    if (retval.first.sh_type == SHT_SYMTAB)
      return retval;
  }
  throw exception(errc::symtab_not_found);
}
} // namespace

namespace tep::dbg {
struct object_info::impl {
  executable_header header;
  std::vector<function_symbol> function_symbols;
  std::vector<compilation_unit> compilation_units;

  explicit impl(std::string_view path) : impl(ro_file_descriptor{path}) {}

private:
  explicit impl(ro_file_descriptor fd) : impl(elf_descriptor{fd}) {}

  explicit impl(elf_descriptor elf) : header({elf}) {
    load_function_symbols(elf);
    load_debug_info(dwarf_descriptor{elf});
  }

  void load_function_symbols(elf_descriptor &);
  void load_debug_info(dwarf_descriptor);
};

void object_info::impl::load_function_symbols(elf_descriptor &elf) {
  auto [header, scn] = find_symtab(elf);
  size_t entry_count = header.sh_size / header.sh_entsize;
  for (Elf_Data *data = elf_getdata(scn, nullptr); data;
       data = elf_getdata(scn, data)) {
    for (size_t i = 0; i < entry_count; ++i) {
      GElf_Sym sym;
      if (!gelf_getsym(data, i, &sym))
        throw exception(elf_errno(), elf_category());
      if (GELF_ST_TYPE(sym.st_info) == STT_FUNC &&
          GELF_ST_BIND(sym.st_info) <= STB_WEAK && sym.st_shndx != SHN_UNDEF) {
        function_symbols.emplace_back(function_symbol::param{elf, header, sym});
      }
    }
  }
  std::sort(function_symbols.begin(), function_symbols.end(),
            [](const function_symbol &lhs, const function_symbol &rhs) {
              if (lhs.name < rhs.name)
                return true;
              if (lhs.name == rhs.name)
                return lhs.address < rhs.address;
              return false;
            });
}

void object_info::impl::load_debug_info(dwarf_descriptor dbg) {
  Dwarf_Off offset = 0;
  while (true) {
    size_t hdr_size;
    Dwarf_Off prev_offset = offset;
    int res = dwarf_nextcu(dbg.value, offset, &offset, &hdr_size, nullptr,
                           nullptr, nullptr);
    if (res == -1)
      throw exception(dwarf_errno(), dwarf_category());
    if (res != 0)
      break;
    Dwarf_Die cu_die;
    if (!dwarf_offdie(dbg.value, prev_offset + hdr_size, &cu_die))
      throw exception(dwarf_errno(), dwarf_category());
    compilation_units.emplace_back(compilation_unit::param{cu_die});
  }
}

object_info::object_info(std::string_view path)
    : impl_(std::make_shared<impl>(path)) {}

const executable_header &object_info::header() const noexcept {
  return impl_->header;
}

const std::vector<function_symbol> &
object_info::function_symbols() const noexcept {
  return impl_->function_symbols;
}

const std::vector<compilation_unit> &
object_info::compilation_units() const noexcept {
  return impl_->compilation_units;
}
} // namespace tep::dbg
