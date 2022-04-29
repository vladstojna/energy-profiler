#include "trap_types.hpp"
#include "dbg/demangle.hpp"
#include "dbg/dwarf.hpp"
#include "dbg/elf.hpp"
#include "output/output_writer.hpp"

#include <util/concat.hpp>

#include <charconv>
#include <cstring>

namespace {
std::string address_to_hex_string(uintptr_t addr) {
  char str[160];
  str[0] = '0';
  str[1] = 'x';
  auto [ptr, err] = std::to_chars(std::begin(str) + 2, std::end(str), addr, 16);
  if (auto ec = make_error_code(err))
    throw std::system_error(ec, "Error converting address to hex string");
  return std::string(str, ptr);
}
} // namespace

namespace tep {
namespace dbg {
static void to_json(nlohmann::json &j, const symbol_binding &x) {
  switch (x) {
  case symbol_binding::global:
    j = "global";
    return;
  case symbol_binding::local:
    j = "local";
    return;
  case symbol_binding::weak:
    j = "weak";
    return;
  }
  assert(false);
}

static void to_json(nlohmann::json &j, const compilation_unit &x) {
  j["path"] = x.path.native();
}

static void to_json(nlohmann::json &j, const source_line &x) {
  j["file"] = x.file.native();
  j["number"] = x.number;
  j["column"] = x.column;
  j["new_statement"] = x.new_statement;
}

static void to_json(nlohmann::json &j, const source_location &x) {
  j["file"] = x.file.native();
  j["line"] = x.line_number;
  j["column"] = x.line_column;
}

static void to_json(nlohmann::json &j, const function &x) {
  j["static"] = x.is_static();
  if (x.decl_loc)
    j["declared"] = *x.decl_loc;
  else
    j["declared"] = nullptr;
}

static void to_json(nlohmann::json &j, const inline_instance &x) {
  if (x.call_loc)
    j["called"] = *x.call_loc;
  else
    j["called"] = nullptr;
}

static void to_json(nlohmann::json &j, const function_symbol &x) {
  j["address"] = address_to_hex_string(x.address);
  j["size"] = address_to_hex_string(x.size);
  j["local_entrypoint"] = address_to_hex_string(x.local_entrypoint());
  j["mangled_name"] = x.name;
  j["demangled_name"] = demangle(x.name);
  j["binding"] = x.binding;
}
} // namespace dbg

static void to_json(nlohmann::json &j, const address &x) {
  j["address"] = address_to_hex_string(x.value);
  if (x.cu)
    j["compilation_unit"] = *x.cu;
  else
    j["compilation_unit"] = nullptr;
}

static void to_json(nlohmann::json &j, const source_line &x) {
  to_json(j, static_cast<const address &>(x));
  j["line"] = *x.line;
}

static void to_json(nlohmann::json &j, const function_call &x) {
  to_json(j, static_cast<const address &>(x));
  nlohmann::json jfunc;
  if (x.sym)
    jfunc["symbol"] = *x.sym;
  else
    jfunc["symbol"] = nullptr;
  jfunc["function"] = *x.func;
  j["function_call"] = std::move(jfunc);
}

static void to_json(nlohmann::json &j, const function_return &x) {
  j["function_return"] = true;
  j["absolute_address"] = address_to_hex_string(x.value);
  if (x.cu)
    j["compilation_unit"] = *x.cu;
  else
    j["compilation_unit"] = nullptr;
}

static void to_json(nlohmann::json &j, const inline_function &x) {
  to_json(j, static_cast<const address &>(x));
  nlohmann::json jfunc;
  if (x.sym)
    jfunc["symbol"] = *x.sym;
  else
    jfunc["symbol"] = nullptr;
  jfunc["function"] = *x.func;
  jfunc["instance"] = *x.inst;
  j["inlined_call"] = std::move(jfunc);
}
} // namespace tep

namespace tep {
bool address::is_function_call() const noexcept { return false; }

bool function_call::is_function_call() const noexcept { return true; }

std::string to_string(const address &x) {
  return cmmn::concat("address:", address_to_hex_string(x.value));
}

std::string to_string(const function_call &x) {
  return cmmn::concat("function_call:", address_to_hex_string(x.value));
}

std::string to_string(const function_return &x) {
  return cmmn::concat("function_return:", address_to_hex_string(x.value));
}

std::string to_string(const inline_function &x) {
  return cmmn::concat("inline_function:", address_to_hex_string(x.value));
}

std::string to_string(const source_line &x) {
  return cmmn::concat("source_line:", address_to_hex_string(x.value));
}

std::ostream &operator<<(std::ostream &os, const address &x) {
  os << to_string(x);
  return os;
}

std::ostream &operator<<(std::ostream &os, const function_call &x) {
  os << to_string(x);
  return os;
}

std::ostream &operator<<(std::ostream &os, const function_return &x) {
  os << to_string(x);
  return os;
}

std::ostream &operator<<(std::ostream &os, const inline_function &x) {
  os << to_string(x);
  return os;
}

std::ostream &operator<<(std::ostream &os, const source_line &x) {
  os << to_string(x);
  return os;
}

output_writer &operator<<(output_writer &ow, const address &x) {
  ow.json = x;
  return ow;
}

output_writer &operator<<(output_writer &ow, const function_call &x) {
  ow.json = x;
  return ow;
}

output_writer &operator<<(output_writer &ow, const function_return &x) {
  ow.json = x;
  return ow;
}

output_writer &operator<<(output_writer &ow, const inline_function &x) {
  ow.json = x;
  return ow;
}

output_writer &operator<<(output_writer &ow, const source_line &x) {
  ow.json = x;
  return ow;
}
} // namespace tep
