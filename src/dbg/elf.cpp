#include "elf.hpp"
#include "common.hpp"
#include "error.hpp"
#include "params_structs.hpp"

#include <cassert>

namespace {
#if defined(__x86_64__) || defined(__i386__)
bool assert_st_other(uint8_t) { return true; }
#elif defined(__powerpc64__)
bool assert_st_other(uint8_t st_other) {
  uint8_t masked = (st_other & STO_PPC64_LOCAL_MASK) >> STO_PPC64_LOCAL_BIT;
  return masked <= 6;
}
#else
#error Unsupported architecture detected
#endif
} // namespace

namespace tep::dbg {
executable_header::executable_header(param x) {
  GElf_Ehdr hdr;
  if (!gelf_getehdr(x.elf.value, &hdr))
    throw exception(elf_errno(), elf_category());
  entrypoint_address = hdr.e_entry;
  switch (hdr.e_type) {
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

function_symbol::function_symbol(param x)
    : name(elf_strptr(x.elf.value, x.header.sh_link, x.sym.st_name)),
      address(x.sym.st_value), size(x.sym.st_size), st_other(x.sym.st_other) {
  assert(GELF_ST_TYPE(x.sym.st_info) == STT_FUNC);
  if (!assert_st_other(x.sym.st_other))
    throw exception(errc::invalid_other_field_value);
  switch (GELF_ST_VISIBILITY(x.sym.st_other)) {
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

  switch (GELF_ST_BIND(x.sym.st_info)) {
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

uintptr_t function_symbol::global_entrypoint() const noexcept {
  return address;
}

#if defined(__x86_64__) || defined(__i386__)
uintptr_t function_symbol::local_entrypoint() const noexcept {
  return global_entrypoint();
}
#elif defined(__powerpc64__)
uintptr_t function_symbol::local_entrypoint() const noexcept {
  // specification: https://files.openpower.foundation/s/aqwWeS3qmoaDyos
  // page 77
  auto local_entry_point_offset = [](uint8_t st_other) {
    return PPC64_LOCAL_ENTRY_OFFSET(st_other);
  };
  return global_entrypoint() + local_entry_point_offset(st_other);
}
#else
#error Unsupported architecture detected
#endif
} // namespace tep::dbg
