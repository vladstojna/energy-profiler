// concat.hpp

#pragma once

#include <string>
#include <string_view>

namespace cmmn {

template <typename Ret = std::string, typename... T> Ret concat(T &&...args) {
  Ret result;
  typename Ret::size_type total_sz = 0;
  std::basic_string_view<typename Ret::value_type> views[] = {args...};
  for (const auto &v : views)
    total_sz += v.size();
  result.reserve(total_sz);
  for (const auto &v : views)
    result.append(v);
  return result;
}

} // namespace cmmn
