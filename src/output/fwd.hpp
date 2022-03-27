#pragma once

#include <iosfwd>

namespace tep
{
    struct output_writer;
    std::ostream& operator<<(std::ostream&, const output_writer&);
} // namespace tep
