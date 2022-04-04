#include "dwarf.hpp"
#include "elf.hpp"
#include "object_info.hpp"

#include <iostream>

namespace tep::dbg {

std::ostream &operator<<(std::ostream &os, executable_type x) {
  switch (x) {
  case executable_type::executable:
    os << "EXEC";
    break;
  case executable_type::shared_object:
    os << "DYN";
    break;
  }
  return os;
}

std::ostream &operator<<(std::ostream &os, symbol_visibility x) {
  switch (x) {
  case symbol_visibility::def:
    os << "default";
    break;
  case symbol_visibility::hidden:
    os << "hidden";
    break;
  case symbol_visibility::internal:
    os << "internal";
    break;
  case symbol_visibility::prot:
    os << "protected";
    break;
  }
  return os;
}

std::ostream &operator<<(std::ostream &os, symbol_binding x) {
  switch (x) {
  case symbol_binding::local:
    os << "local";
    break;
  case symbol_binding::global:
    os << "global";
    break;
  case symbol_binding::weak:
    os << "weak";
    break;
  }
  return os;
}

std::ostream &operator<<(std::ostream &os, const executable_header &x) {
  std::ios::fmtflags flags(os.flags());
  os << x.type << ":" << std::hex << x.entrypoint_address;
  os.flags(flags);
  return os;
}

std::ostream &operator<<(std::ostream &os, const function_symbol &x) {
  std::ios::fmtflags flags(os.flags());
  os << x.name << "|" << std::hex << x.address << "|" << x.size << "|"
     << x.visibility << "|" << x.binding;
  os.flags(flags);
  return os;
}

std::ostream &operator<<(std::ostream &os, line_context x) {
  switch (x) {
  case line_context::prologue_end:
    os << "prologue-end";
    break;
  case line_context::epilogue_begin:
    os << "epilogue-begin";
    break;
  case line_context::none:
    os << "none";
    break;
  }
  return os;
}

std::ostream &operator<<(std::ostream &os, const contiguous_range &x) {
  std::ios::fmtflags flags(os.flags());
  os << std::hex << x.low_pc << "-" << x.high_pc;
  os.flags(flags);
  return os;
}

std::ostream &operator<<(std::ostream &os, const source_line &x) {
  std::ios::fmtflags flags(os.flags());
  os << (void *)x.address << "@";
  os << x.file.native() << ":" << x.number << ":" << x.column;
  os << ",";
  os << "new_statement=" << std::boolalpha << x.new_statement;
  os << ",";
  os << "new_basic_block=" << std::boolalpha << x.new_basic_block;
  os << ",";
  os << "end_text_sequence=" << std::boolalpha << x.end_text_sequence;
  os << ",";
  os << "context=" << x.ctx;
  os.flags(flags);
  return os;
}

std::ostream &operator<<(std::ostream &os, const source_location &x) {
  os << x.file.native() << ":" << x.line_number << ":" << x.line_column;
  return os;
}

std::ostream &operator<<(std::ostream &os, const function_addresses &x) {
  os << "Ranges:";
  for (const auto &r : x.values)
    os << " " << r;
  return os;
}

std::ostream &operator<<(std::ostream &os, const inline_instance &x) {
  os << "  Called: ";
  if (x.call_loc)
    os << *x.call_loc;
  else
    os << "n/a";
  os << "\n";
  os << "  Entry PC: ";
  std::ios::fmtflags flags{os.flags()};
  if (x.entry_pc)
    os << std::hex << x.entry_pc;
  else
    os << "n/a";
  os.flags(flags);
  os << "\n";
  os << "  " << x.addresses;
  return os;
}

std::ostream &operator<<(std::ostream &os, const inline_instances &x) {
  os << "Inline instances (" << x.insts.size() << "):";
  for (const auto &x : x.insts)
    os << "\n" << x;
  return os;
}

std::ostream &operator<<(std::ostream &os, const function &x) {
  os << "DIE: " << x.die_name << "\n";
  os << "Declared: ";
  if (x.decl_loc)
    os << *x.decl_loc;
  else
    os << "n/a";
  os << "\n";
  os << "Linkage name: ";
  if (x.linkage_name)
    os << *x.linkage_name;
  else
    os << "n/a";
  os << "\n";
  if (x.addresses)
    os << *x.addresses << "\n";
  if (x.instances)
    os << *x.instances << "\n";
  return os;
}

std::ostream &operator<<(std::ostream &os, const compilation_unit &x) {
  os << x.path.native() << "\n";
  for (const auto &r : x.addresses)
    os << r << "\n";
  for (const auto &l : x.lines)
    os << l << "\n";
  for (const auto &f : x.funcs)
    os << f << "\n";
  return os;
}

std::ostream &operator<<(std::ostream &os, const object_info &x) {
  os << x.header() << "\n";
  for (const auto &f : x.function_symbols())
    os << f << "\n";
  for (const auto &cu : x.compilation_units())
    os << cu << "\n";
  return os;
}
} // namespace tep::dbg
