// idle_evaluator.hpp

#pragma once

#include "periodic_sampler.hpp"

namespace cmmn
{
    template<typename R, typename E>
    class expected;
}

namespace tep
{

    class tracer_error;

    class idle_evaluator
    {
    public:
        static struct reserve_tag {} reserve;

    private:
        static std::chrono::seconds default_sleep_duration;

    private:
        std::chrono::seconds _sleep;
        periodic_sampler _sampler;

        void idle();

    public:
        idle_evaluator(const nrgprf::reader* reader,
            const std::chrono::seconds& sleep_for = default_sleep_duration);

        idle_evaluator(reserve_tag,
            const nrgprf::reader* reader,
            const std::chrono::seconds& sleep_for = default_sleep_duration);

        cmmn::expected<nrgprf::execution, tracer_error> run();
    };

}
