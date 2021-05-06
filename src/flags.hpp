// flags.hpp

#pragma once

#include <iosfwd>

namespace tep
{

    class flags
    {
    private:
        bool _obtain_idle;

    public:
        explicit flags(bool idle);

        bool obtain_idle_readings() const;
    };

    std::ostream& operator<<(std::ostream& os, const flags& f);

}
