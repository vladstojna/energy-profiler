#include <nrg/nrg.hpp>
#include <nonstd/expected.hpp>

#include <thread>
#include <utility>

namespace
{
    using readings = std::pair<
        nrgprf::sensor_value::time_point,
        nrgprf::watts<double>
    >;

    template<typename Location>
    std::pair<readings, readings> get_readings(
        const nrgprf::reader_rapl& reader,
        const nrgprf::sample& first,
        const nrgprf::sample& last,
        uint8_t socket)
    {
        using nrgprf::exception;
        auto readings_first = reader.value<Location>(first, socket);
        if (!readings_first)
            throw exception(readings_first.error());
        auto readings_last = reader.value<Location>(last, socket);
        if (!readings_last)
            throw exception(readings_last.error());
        return {
            { readings_first->timestamp, readings_first->power },
            { readings_last->timestamp, readings_last->power }
        };
    }
}

int main()
{
    try
    {
        using namespace nrgprf;
        constexpr uint8_t socket = 0;

        reader_rapl reader(locmask::pkg, 0x1);
        sample first;
        sample last;
        if (std::error_code ec; !reader.read(first, ec))
            throw exception(ec);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (std::error_code ec; !reader.read(last, ec))
            throw exception(ec);
        auto [before, after] =
            get_readings<loc::pkg>(reader, first, last, socket);

        std::cout << "Before sleep: " << before.second.count()
            << " W @ " << before.first.time_since_epoch().count() << "\n";
        std::cout << "After sleep: " << after.second.count()
            << " W @ " << after.first.time_since_epoch().count() << "\n";

        auto delta = std::chrono::duration<double>{ after.first - before.first };
        std::cout << "Time between samples: " << delta.count() << " s\n";
        if (!delta.count())
        {
            std::cout << "Average power: n/a (samples are the same)\n";
            std::cout << "Energy consumed: n/a (samples are the same)\n";
        }
        else
        {
            std::cout << "Average power: "
                << ((after.second + before.second) / 2).count() << " W\n";
            std::cout << "Energy consumed: "
                << ((after.second + before.second) / 2 * delta).count() << " J\n";
        }
    }
    catch (const nrgprf::exception& e)
    {
        std::cerr << "NRG exception: " << e.what() << '\n';
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
