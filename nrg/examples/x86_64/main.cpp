#include "../common/exception.hpp"

#include <nrg/nrg.hpp>
#include <nonstd/expected.hpp>

#include <thread>
#include <utility>

namespace
{
    template<typename Location>
    std::pair<nrgprf::joules<double>, nrgprf::joules<double>>
        get_readings(
            const nrgprf::reader_rapl& reader,
            const nrgprf::sample& first,
            const nrgprf::sample& last,
            uint8_t socket)
    {
        using example::nrg_exception;
        auto energy_first = reader.value<Location>(first, socket);
        if (!energy_first)
            throw nrg_exception(energy_first.error());
        auto energy_last = reader.value<Location>(last, socket);
        if (!energy_last)
            throw nrg_exception(energy_last.error());
        return { *energy_first, *energy_last };
    }
}

int main()
{
    try
    {
        using namespace nrgprf;
        using example::nrg_exception;

        constexpr uint8_t socket = 0;

        error err = error::success();
        reader_rapl reader(locmask::pkg, 0x1, err);
        if (err)
            throw nrg_exception(err);

        sample first;
        sample last;
        if (auto err = reader.read(first))
            throw nrg_exception(err);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (auto err = reader.read(last))
            throw nrg_exception(err);
        auto [before, after] =
            get_readings<loc::pkg>(reader, first, last, socket);

        std::cout << "Before sleep: " << before.count() << " J\n";
        std::cout << "After sleep: " << after.count() << " J\n";
        std::cout << "Energy consumed: " << (after - before).count() << " J\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
