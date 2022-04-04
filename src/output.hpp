// output.hpp

#pragma once

#include "timed_sample.hpp"
#include "trap_context.hpp"
#include "output/fwd.hpp"

#include <optional>

namespace tep
{
    struct position_exec
    {
        std::pair<trap_context, trap_context> interval;
        timed_execution exec;
    };

    class readings_output
    {
    public:
        virtual ~readings_output() = default;
        virtual void output(output_writer& os, const timed_execution& exec) const = 0;
    };

    class readings_output_holder final : public readings_output
    {
    private:
        std::vector<std::unique_ptr<readings_output>> _outputs;

    public:
        readings_output_holder() = default;
        void push_back(std::unique_ptr<readings_output>&& outputs);
        void output(output_writer& os, const timed_execution& exec) const override;
    };

    template<typename Reader>
    class readings_output_dev : public readings_output
    {
    private:
        Reader _reader;

    public:
        readings_output_dev(const Reader& reader);

        void output(output_writer& os, const timed_execution& exec) const override;
    };

    class idle_output
    {
    private:
        std::unique_ptr<readings_output> _rout;
        timed_execution _exec;

    public:
        idle_output(std::unique_ptr<readings_output>&& rout, timed_execution&& exec);

        timed_execution& exec();
        const timed_execution& exec() const;
        const readings_output& readings_out() const;
    };

    class section_output
    {
    private:
        std::unique_ptr<readings_output> _rout;
        std::optional<std::string> _label;
        std::optional<std::string> _extra;
        std::vector<position_exec> _executions;

    public:
        section_output(
            std::unique_ptr<readings_output> rout,
            std::optional<std::string_view> label,
            std::optional<std::string_view> extra);

        position_exec& push_back(position_exec&& pe);

        const readings_output& readings_out() const;
        const std::optional<std::string>& label() const;
        const std::optional<std::string>& extra() const;
        const std::vector<position_exec>& executions() const;
    };

    class group_output
    {
    public:
        using container = std::vector<section_output>;

    private:
        std::optional<std::string> _label;
        std::optional<std::string> _extra;
        container _sections;

    public:
        group_output(
            std::optional<std::string_view> label,
            std::optional<std::string_view> extra);

        section_output& push_back(section_output&& so);

        const std::optional<std::string>& label() const;
        const std::optional<std::string>& extra() const;

        container& sections();
        const container& sections() const;
    };

    class profiling_results
    {
    public:
        using container = std::vector<group_output>;

    private:
        std::vector<idle_output> _idle;
        container _results;

    public:
        profiling_results() = default;

        std::vector<idle_output>& idle();
        const std::vector<idle_output>& idle() const;

        container& groups();
        const container& groups() const;
    };

    // operator overloads

    std::ostream& operator<<(std::ostream& os, const profiling_results& pr);

    // deduction guides

    template<typename Reader>
    readings_output_dev(const Reader& r)->readings_output_dev<Reader>;

    using readings_output_cpu = readings_output_dev<nrgprf::reader_rapl>;
    using readings_output_gpu = readings_output_dev<nrgprf::reader_gpu>;

}
