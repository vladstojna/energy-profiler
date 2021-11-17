#include <nrg/error.hpp>

#include <stdexcept>

namespace example
{
    class nrg_exception : public std::exception
    {
        nrgprf::error _err;

    public:
        nrg_exception(const nrgprf::error& err) :
            _err(err)
        {}

        const char* what() const noexcept override
        {
            return _err.msg().c_str();
        }
    };
}
