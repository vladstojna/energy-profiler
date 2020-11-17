// energy_reader.h
#pragma once

#include <chrono>
#include <iosfwd>
#include <stdexcept>
#include <memory>
namespace tep::energy
{

enum class target
{
    smp,
    gpu,
    fpga
};

enum class engine
{
    papi,
    pcm
};

}

namespace tep
{

constexpr const size_t ISAMPLE_SIZE = 128;

class energy_reader_exception : public std::runtime_error
{
public:
    energy_reader_exception(const char* what) :
        std::runtime_error(what) {}
    energy_reader_exception(const std::string& what) :
        std::runtime_error(what) {}
};

class energy_reader
{
protected:
    using timepoint_t = std::chrono::time_point<std::chrono::high_resolution_clock>;
    struct basic_sample
    {
        const uint64_t number;
        const timepoint_t timepoint;

        basic_sample(uint64_t count, const timepoint_t tp) :
            number(count), timepoint(tp)
        {
        }

        std::chrono::nanoseconds operator-(const basic_sample& rhs) const
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                timepoint - rhs.timepoint);
        }
    };

public:
    virtual ~energy_reader() = default;
    virtual void start() = 0;
    virtual void sample() = 0;
    virtual void stop() = 0;

    friend std::ostream& operator<<(std::ostream& os, const energy_reader& er)
    {
        er.print(os);
        return os;
    }

protected:
    // count,timestamp,skt0_cpu,skt0_dram,...
    virtual void print(std::ostream& os) const = 0;

    /**
    * returns the timepoint that represents the time now
    **/
    timepoint_t now()
    {
        return std::chrono::high_resolution_clock::now();
    }
};

std::unique_ptr<energy_reader> make_energy_reader(
    energy::target target,
    energy::engine engine = energy::engine::papi);

}
