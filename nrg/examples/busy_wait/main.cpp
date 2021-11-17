#include "../common/exception.hpp"

#include <nrg/nrg.hpp>
#include <nonstd/expected.hpp>

#include <charconv>
#include <stdexcept>
#include <thread>

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

    template<typename Location>
    nrgprf::joules<double> total_energy(
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
        return *energy_last - *energy_first;
    }

    nrgprf::joules<double> calibrate_busy_wait(
        const nrgprf::reader_rapl& reader,
        std::uint8_t socket = 0,
        std::size_t iters = 1000000)
    {
        using namespace nrgprf;
        using example::nrg_exception;
        std::cout << "Calibrating busy wait parameters\n";

        sample first;
        sample last;

        if (auto err = reader.read(first))
            throw nrg_exception(err);
        for (auto [i, s] = std::pair{ std::size_t{}, sample{} }; i < iters; i++)
        {
            if (auto err = reader.read(s))
                throw nrg_exception(err);
        }
        if (auto err = reader.read(last))
            throw nrg_exception(err);

        auto energy_first = reader.value<loc::pkg>(first, socket);
        if (!energy_first)
            throw nrg_exception(energy_first.error());
        auto energy_last = reader.value<loc::pkg>(last, socket);
        if (!energy_last)
            throw nrg_exception(energy_last.error());

        joules<double> consumed = total_energy<loc::pkg>(reader, first, last, socket);
        joules<double> per_iter = consumed / iters;

        std::cout << "iterations: " << iters << "\n";
        std::cout << "total energy: " << consumed.count() << " J\n";
        std::cout << "energy per iteration: " << per_iter.count() << " J\n";

        return per_iter;
    }

    std::pair<std::size_t, nrgprf::sample> wait(const nrgprf::reader_rapl& reader)
    {
        using namespace nrgprf;
        using example::nrg_exception;
        sample first;
        sample last;
        std::size_t iters = 0;

        if (auto err = reader.read(first))
            throw nrg_exception(err);
        do
        {
            iters++;
            if (auto err = reader.read(last))
                throw nrg_exception(err);
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
        using example::nrg_exception;

        constexpr uint8_t socket = 0;
        const arguments args(argc, argv);

        error err = error::success();
        reader_rapl reader(locmask::pkg, 0x1, err);
        if (err)
            throw nrg_exception(err);

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
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
