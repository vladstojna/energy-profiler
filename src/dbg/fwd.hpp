#pragma once

#include <cstdint>

namespace tep::dbg {
enum class executable_type : uint32_t;
enum class symbol_visibility : uint32_t;
enum class symbol_binding : uint32_t;
struct executable_header;
struct function_symbol;

enum class line_context : uint32_t;
struct contiguous_range;
struct source_line;
struct source_location;
struct function_addresses;
struct inline_instance;
struct inline_instances;
struct function;
struct compilation_unit;

struct object_info;
} // namespace tep::dbg
