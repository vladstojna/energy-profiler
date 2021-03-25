// trap.hpp

#pragma once

#include <cstdint>
#include <set>

#include "config.hpp"

namespace tep
{

    class trap_data
    {
    private:
        uintptr_t _addr;
        long _origw;
        const config_data::section* _section;

    public:
        trap_data(uintptr_t addr, long ow, const config_data::section& sec);

        uintptr_t address() const;
        long original_word() const;
        const config_data::section& section() const;
    };

    bool operator<(const trap_data& lhs, const trap_data& rhs);
    bool operator<(uintptr_t lhs, const trap_data& rhs);
    bool operator<(const trap_data& lhs, uintptr_t rhs);

    using trap_set = std::set<trap_data, std::less<>>;

}
