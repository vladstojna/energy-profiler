#include <nrg/nrg.hpp>
#include <nonstd/expected.hpp>

#include <charconv>
#include <stdexcept>
#include <thread>

// Example using a busy wait/polling technique, similar to the one
// described in https://dl.acm.org/doi/10.1145/2425248.2425252

// Usage: ./main.out [microseconds sleep] [calibration iterations]

namespace
{
    template<typename T>
    T to_scalar(std::string_view str)
    {
        T value;
        auto [dummy, ec] = std::from_chars(str.begin(), str.end(), value);
        (void)dummy;
        if (auto code = std::make_error_code(ec))
            throw std::system_error(code);
        return value;
    }

    // Get the energy consumed from two readings, i.e., subtract both samples.
    template<typename Location>
    nrgprf::joules<double> total_energy(
        const nrgprf::reader_rapl& reader,
        const nrgprf::sample& first,
        const nrgprf::sample& last,
        uint8_t socket)
    {
        using nrgprf::exception;
        auto energy_first = reader.value<Location>(first, socket);
        if (!energy_first)
            throw exception(energy_first.error());
        auto energy_last = reader.value<Location>(last, socket);
        if (!energy_last)
            throw exception(energy_last.error());
        return *energy_last - *energy_first;
    }

    // Calibrates the busy wait technique by executing a loop <iters> times.
    // The idea is to get the energy consumed per iteration and
    // use this value when subtracting from the total energy consumed.
    nrgprf::joules<double> calibrate_busy_wait(
        const nrgprf::reader_rapl& reader,
        std::uint8_t socket = 0,
        std::size_t iters = 1000000)
    {
        using namespace nrgprf;
        std::cout << "Calibrating busy wait parameters\n";

        sample first;
        sample last;

        if (std::error_code ec; !reader.read(first, ec))
            throw exception(ec);
        for (auto [i, s] = std::pair{ std::size_t{}, sample{} }; i < iters; i++)
        {
            if (std::error_code ec; !reader.read(s, ec))
                throw exception(ec);
        }
        if (std::error_code ec; !reader.read(last, ec))
            throw exception(ec);

        auto energy_first = reader.value<loc::pkg>(first, socket);
        if (!energy_first)
            throw exception(energy_first.error());
        auto energy_last = reader.value<loc::pkg>(last, socket);
        if (!energy_last)
            throw exception(energy_last.error());

        joules<double> consumed = total_energy<loc::pkg>(reader, first, last, socket);
        joules<double> per_iter = consumed / iters;

        std::cout << "iterations: " << iters << "\n";
        std::cout << "total energy: " << consumed.count() << " J\n";
        std::cout << "energy per iteration: " << per_iter.count() << " J\n";

        return per_iter;
    }

    // Implementation of the busy polling.
    // Samples the sensors and then loops until the value read changes, which implies
    // that the sensor has refreshed.
    // Returns the last sample read and the number of iterations the sensor took to refresh.
    std::pair<std::size_t, nrgprf::sample> wait(const nrgprf::reader_rapl& reader)
    {
        using nrgprf::sample;
        using nrgprf::exception;
        sample first;
        sample last;
        std::size_t iters = 0;

        if (std::error_code ec; !reader.read(first, ec))
            throw exception(ec);
        do
        {
            iters++;
            if (std::error_code ec; !reader.read(last, ec))
                throw exception(ec);
        } while (first == last);
        return { iters, last };
    }

    struct arguments
    {
        std::uint32_t sleep_for = 5;
        std::size_t iters = 1000000;

        arguments(int argc, const char* const* argv)
        {
            if (argc < 2)
            {
                usage(argv[0]);
                throw std::runtime_error("Not enough arguments");
            }
            sleep_for = to_scalar<decltype(sleep_for)>(argv[1]);
            if (argc > 2)
                iters = to_scalar<decltype(iters)>(argv[2]);
        }

    private:
        void usage(const char* prog)
        {
            std::cerr << "Usage: " << prog << " [microseconds] [calibration iters]\n";
        }
    };
}

int main(int argc, char** argv)
{
    try
    {
        using namespace nrgprf;
        constexpr uint8_t socket = 0;
        const arguments args(argc, argv);

        reader_rapl reader(locmask::pkg, 0x1);

        auto calibrated_val = calibrate_busy_wait(reader, socket, args.iters);

        auto [ib, first] = wait(reader);
        std::this_thread::sleep_for(std::chrono::microseconds(args.sleep_for));
        auto [ia, last] = wait(reader);

        std::cout << "Iterations before: " << ib << "\n";
        std::cout << "Iterations after: " << ia << "\n";

        auto consumed = total_energy<loc::pkg>(reader, first, last, socket);
        std::cout << "Total energy: " << consumed.count() << " J\n";
        std::cout << "Wait subtracted: " << (consumed - calibrated_val * ia).count() << " J\n";
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
