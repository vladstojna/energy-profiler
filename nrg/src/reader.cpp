#include <nonstd/expected.hpp>
#include <nrg/reader.hpp>
#include <nrg/sample.hpp>

namespace nrgprf {
void reader::read(sample &s) const {
  if (std::error_code ec; !read(s, ec))
    throw exception(ec);
}

void reader::read(sample &s, uint8_t idx) const {
  if (std::error_code ec; !read(s, idx, ec))
    throw exception(ec);
}

result<sample> reader::read() const {
  sample s;
  if (std::error_code ec; !read(s, ec))
    return nonstd::unexpected<std::error_code>(ec);
  return s;
}

result<sample> reader::read(uint8_t idx) const {
  sample s;
  if (std::error_code ec; !read(s, idx, ec))
    return nonstd::unexpected<std::error_code>(ec);
  return s;
}
} // namespace nrgprf
