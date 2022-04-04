#include "common.hpp"
#include "error.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <system_error>

namespace tep::dbg {
ro_file_descriptor::ro_file_descriptor(std::string_view path)
    : value(open(path.data(), O_RDONLY)) {
  if (value == -1)
    throw std::system_error(errno, std::system_category());
}

ro_file_descriptor::~ro_file_descriptor() {
  if (close(value) < 0) {
    std::cerr << "Error closing file descriptor: " << strerror(errno)
              << std::endl;
  }
}

elf_descriptor::elf_descriptor(ro_file_descriptor &fd) : value(nullptr) {
  if (elf_version(EV_CURRENT) == EV_NONE)
    throw exception(elf_errno(), elf_category());
  if (!(value = elf_begin(fd.value, ELF_C_READ, nullptr)))
    throw exception(elf_errno(), elf_category());
  if (elf_kind(value) != ELF_K_ELF)
    throw exception(errc::not_an_elf_object);
}

elf_descriptor::~elf_descriptor() {
  if (elf_end(value) != 0) {
    std::cerr << "Error closing ELF context: " << elf_errmsg(-1) << std::endl;
  }
}

dwarf_descriptor::dwarf_descriptor(elf_descriptor &elf)
    : value(dwarf_begin_elf(elf.value, DWARF_C_READ, nullptr)) {
  if (!value)
    throw exception(dwarf_errno(), dwarf_category());
}

dwarf_descriptor::~dwarf_descriptor() {
  if (dwarf_end(value) != 0) {
    std::cerr << "Error closing DWARF debug context: " << dwarf_errmsg(-1)
              << std::endl;
  }
}

} // namespace tep::dbg
