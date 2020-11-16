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
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> _timepoint;

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
    * sets timepoint to the time now
    **/
    void timepoint_now()
    {
        _timepoint = std::chrono::high_resolution_clock::now();
    }

    /**
    * sets the timepoint to the time now and returns
    * the duration between it and the previous timepoint
    **/
    std::chrono::milliseconds timepoint_update()
    {
        auto now = std::chrono::high_resolution_clock::now();
        auto retval = std::chrono::duration_cast<std::chrono::milliseconds>(now - _timepoint);
        _timepoint = now;
        return retval;
    }
};

std::unique_ptr<energy_reader> make_energy_reader(
    energy::target target,
    energy::engine engine = energy::engine::papi);

}
