#include "funcs.hpp"

#include <nonstd/expected.hpp>
#include <util/concat.hpp>

#include <cstring>
#include <fstream>
#include <set>

namespace nrgprf
{
    result<uint8_t> count_sockets()
    {
        using rettype = result<uint8_t>;
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
                return rettype(nonstd::unexpect, errno, std::system_category());
            }
            uint32_t pkg;
            if (!(ifs >> pkg))
                return rettype(nonstd::unexpect, errno, std::system_category());
            packages.insert(pkg);
        };
        if (packages.empty())
            return rettype(nonstd::unexpect, errc::no_sockets_found);
        if (packages.size() > max_sockets)
            return rettype(nonstd::unexpect, errc::too_many_sockets);
        return packages.size();
    }
}
