// energy_reader.h
#pragma once

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
    uint64_t _sample_count = 0;

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
};

std::unique_ptr<energy_reader> make_energy_reader(
    energy::target target,
    energy::engine engine = energy::engine::papi);

}
