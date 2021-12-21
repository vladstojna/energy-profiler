#include "funcs.hpp"

#include <nonstd/expected.hpp>
#include <util/concat.hpp>

#include <cstring>
#include <fstream>
#include <set>

namespace nrgprf
{
    std::string system_error_str(std::string_view prefix)
    {
        char buffer[256];
        return cmmn::concat(prefix, ": ", strerror_r(errno, buffer, sizeof(buffer)));
    }

    result<uint8_t> count_sockets()
    {
        using rettype = result<uint8_t>;
        auto system_error = [](std::string&& msg)
        {
            return rettype(nonstd::unexpect, error_code::SYSTEM, system_error_str(msg));
        };
        auto too_many_sockets = [](std::size_t max, std::size_t found)
        {
            return rettype(
                nonstd::unexpect,
                error_code::TOO_MANY_SOCKETS,
                cmmn::concat("Too many sockets: maximum of ", std::to_string(max),
                    ", found ", std::to_string(found))
            );
        };

        char filename[128];
        std::set<uint32_t> packages;
        for (int i = 0; ; i++)
        {
            snprintf(filename, sizeof(filename),
                "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i);
            std::ifstream ifs(filename, std::ios::in);
            if (!ifs)
            {
                // file does not exist
                if (errno == ENOENT)
                    break;
                return system_error(cmmn::concat("Error opening ", filename));
            }
            uint32_t pkg;
            if (!(ifs >> pkg))
                return system_error("Error reading package");
            packages.insert(pkg);
        };
        if (packages.empty())
            return rettype(nonstd::unexpect, error_code::NO_SOCKETS, "No sockets found");
        if (packages.size() > max_sockets)
            return too_many_sockets(max_sockets, packages.size());
        return packages.size();
    }
}
