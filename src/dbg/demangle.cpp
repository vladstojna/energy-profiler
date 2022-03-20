#include "demangle.hpp"

#include <cxxabi.h>
#include <cassert>

namespace
{
    struct demangled_ptr
    {
        char* ptr;

        explicit demangled_ptr(char* ptr) noexcept :
            ptr(ptr)
        {}

        ~demangled_ptr()
        {
            free(ptr);
        }
    };
}

namespace tep
{
    namespace dbg
    {
        std::optional<std::string> demangle(
            std::string_view mangled,
            std::error_code& ec,
            bool demangle_types)
        {
            if (!demangle_types &&
                (mangled.size() < 2 || !(mangled[0] == '_' && mangled[1] == 'Z')))
            {
                ec.clear();
                return std::string(mangled);
            }
            int status;
            size_t length;
            demangled_ptr demangled{
                abi::__cxa_demangle(mangled.data(), nullptr, &length, &status)
            };
            if (!status)
            {
                ec.clear();
                return std::string(demangled.ptr, length);
            }
            else if (status == -1)
                ec = make_error_code(std::errc::not_enough_memory);
            else if (status == -2)
                ec = make_error_code(std::errc::invalid_argument);
            else
            {
                assert(false);
                throw std::logic_error("Invalid status from demangling routine");
            }
            return std::nullopt;
        }
    }
}
