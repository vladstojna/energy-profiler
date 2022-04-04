#include "dump.hpp"
#include "object_info.hpp"

#include <nlohmann/json.hpp>

#include <charconv>

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

namespace tep::dbg {
debug_dump::debug_dump(const object_info &x) noexcept : obj_info(x) {}

static void to_json(nlohmann::json &j, const executable_type &x) {
  switch (x) {
  case executable_type::executable:
    j = "exec";
    break;
  case executable_type::shared_object:
    j = "dyn";
    break;
  }
}

static void to_json(nlohmann::json &j, const symbol_binding &x) {
  switch (x) {
  case symbol_binding::local:
    j = "local";
    break;
  case symbol_binding::weak:
    j = "weak";
    break;
  case symbol_binding::global:
    j = "global";
    break;
  }
}

static void to_json(nlohmann::json &j, const symbol_visibility &x) {
  switch (x) {
  case symbol_visibility::def:
    j = "default";
    break;
  case symbol_visibility::hidden:
    j = "hidden";
    break;
  case symbol_visibility::internal:
    j = "internal";
    break;
  case symbol_visibility::prot:
    j = "protected";
    break;
  }
}

static void to_json(nlohmann::json &j, const executable_header &x) {
  j["executable_type"] = x.type;
  j["entrypoint"] = address_to_hex_string(x.entrypoint_address);
}

static void to_json(nlohmann::json &j, const contiguous_range &x) {
  j["start"] = address_to_hex_string(x.low_pc);
  j["end"] = address_to_hex_string(x.high_pc);
}

static void to_json(nlohmann::json &j, const function_symbol &x) {
  j["address"] = address_to_hex_string(x.address);
  j["local_entrypoint"] = address_to_hex_string(x.local_entrypoint());
  j["size"] = address_to_hex_string(x.size);
  j["name"] = x.name;
  j["binding"] = x.binding;
  j["visibility"] = x.visibility;
}

static void to_json(nlohmann::json &j, const line_context &x) {
  switch (x) {
  case line_context::prologue_end:
    j = "prologue_end";
    break;
  case line_context::none:
    j = "none";
    break;
  case line_context::epilogue_begin:
    j = "epilogue_begin";
    break;
  }
}

static void to_json(nlohmann::json &j, const source_line &x) {
  j["address"] = address_to_hex_string(x.address);
  j["file"] = x.file.native();
  j["number"] = x.number;
  j["column"] = x.column;
  j["new_statement"] = x.new_statement;
  j["new_basic_block"] = x.new_basic_block;
  j["end_text_sequence"] = x.end_text_sequence;
  j["context"] = x.ctx;
}

static void to_json(nlohmann::json &j, const source_location &x) {
  j["file"] = x.file.native();
  j["line"] = x.line_number;
  j["column"] = x.line_column;
}

static void to_json(nlohmann::json &j, const function_addresses &x) {
  j = nlohmann::json::array();
  for (const auto &r : x.values) {
    nlohmann::json range;
    to_json(range, r);
    j.push_back(std::move(range));
  }
}

static void to_json(nlohmann::json &j, const inline_instance &x) {
  if (x.call_loc)
    j["called"] = *x.call_loc;
  j["entry_pc"] = address_to_hex_string(x.entry_pc);
  j["addresses"] = x.addresses;
}

static void to_json(nlohmann::json &j, const inline_instances &x) {
  j = nlohmann::json::array();
  for (const auto &i : x.insts) {
    nlohmann::json inst;
    to_json(inst, i);
    j.push_back(std::move(inst));
  }
}

static void to_json(nlohmann::json &j, const function &x) {
  j["die_name"] = x.die_name;
  j["static"] = x.is_static();
  if (x.decl_loc)
    j["declared"] = *x.decl_loc;
  if (x.linkage_name)
    j["linkage_name"] = *x.linkage_name;
  if (x.addresses)
    j["addresses"] = *x.addresses;
  if (x.instances)
    j["inlined_instances"] = *x.instances;
}

static void to_json(nlohmann::json &j, const compilation_unit &x) {
  j["path"] = x.path.native();
  auto &addrs = j["addresses"] = nlohmann::json::array();
  for (const auto &r : x.addresses) {
    nlohmann::json j;
    to_json(j, r);
    addrs.push_back(std::move(j));
  }
  auto &lines = j["lines"] = nlohmann::json::array();
  for (const auto &l : x.lines) {
    nlohmann::json j;
    to_json(j, l);
    lines.push_back(std::move(j));
  }
  auto &funcs = j["functions"] = nlohmann::json::array();
  for (const auto &f : x.funcs) {
    nlohmann::json j;
    to_json(j, f);
    funcs.push_back(std::move(j));
  }
}

static void to_json(nlohmann::json &j, const object_info &x) {
  j["header"] = x.header();
  auto &syms = j["function_symbols"] = nlohmann::json::array();
  for (const auto &sym : x.function_symbols()) {
    nlohmann::json j;
    to_json(j, sym);
    syms.push_back(std::move(j));
  }
  auto &cus = j["compilation_units"] = nlohmann::json::array();
  for (const auto &cu : x.compilation_units()) {
    nlohmann::json j;
    to_json(j, cu);
    cus.push_back(std::move(j));
  }
}
} // namespace tep::dbg

namespace tep::dbg {
std::ostream &operator<<(std::ostream &os, const debug_dump &x) {
  nlohmann::json j;
  j = x.obj_info;
  os << j;
  return os;
}
} // namespace tep::dbg
