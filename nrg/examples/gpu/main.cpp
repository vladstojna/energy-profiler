#include <nrg/nrg.hpp>
#include <nonstd/expected.hpp>

#include <thread>
#include <utility>

namespace
{
    struct power_query
    {
        using unit = nrgprf::watts<double>;

        static unit value(
            const nrgprf::reader_gpu& r,
            const nrgprf::sample& s,
            uint32_t dev)
        {
            using nrgprf::exception;
            auto val = r.get_board_power(s, dev);
            if (!val)
                throw exception(val.error());
            return unit{ *val };
        }
    };

    struct energy_query
    {
        using unit = nrgprf::joules<double>;

        static unit value(
            const nrgprf::reader_gpu& r,
            const nrgprf::sample& s,
            uint32_t dev)
        {
            using nrgprf::exception;
            auto val = r.get_board_energy(s, dev);
            if (!val)
                throw exception(val.error());
            return unit{ *val };
        }
    };

    template<typename T>
    std::pair<typename T::unit, typename T::unit>
        get_readings(
            const nrgprf::reader_gpu& reader,
            const nrgprf::sample& first,
            const nrgprf::sample& last,
            uint8_t dev)
    {
        auto val_first = T::value(reader, first, dev);
        auto val_last = T::value(reader, last, dev);
        return { val_first, val_last };
    }

    std::ostream& operator<<(std::ostream& os, nrgprf::readings_type::type rt)
    {
        using namespace nrgprf;
        if (rt & readings_type::power)
            os << "Device supports power readings";
        if (rt & readings_type::energy)
        {
            if (rt & readings_type::power)
                os << "\n";
            os << "Device supports energy readings";
        }
        return os;
    }
}

int main()
{
    try
    {
        using namespace nrgprf;
        constexpr uint8_t device = 0;

        auto support = reader_gpu::support(0x1);
        if (!support)
            throw exception(support.error());
        std::cout << *support << "\n";

        reader_gpu reader(*support, 0x1);

        sample first;
        sample last;
        if (std::error_code ec; !reader.read(first, ec))
            throw exception(ec);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (std::error_code ec; !reader.read(last, ec))
            throw exception(ec);

        if (*support & readings_type::energy)
        {
            std::cout << "--- Energy ---\n";
            auto [before, after] =
                get_readings<energy_query>(reader, first, last, device);
            std::cout << "Before sleep: " << before.count() << " J\n";
            std::cout << "After sleep: " << after.count() << " J\n";
            std::cout << "Consumed: " << (after - before).count() << " J\n";
        }
        if (*support & readings_type::power)
        {
            std::cout << "--- Power ---\n";
            auto [before, after] =
                get_readings<power_query>(reader, first, last, device);
            std::cout << "Before sleep: " << before.count() << " W\n";
            std::cout << "After sleep: " << after.count() << " W\n";
            std::cout << "Average: " << ((after + before) / 2).count() << " W\n";
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
