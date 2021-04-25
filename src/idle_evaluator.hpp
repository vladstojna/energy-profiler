// idle_evaluator.hpp

#pragma once

#include "reader_container.hpp"

namespace cmmn
{
    template<typename R, typename E>
    class expected;
}

namespace tep
{

    struct idle_results;

    class tracer_error;

    class idle_evaluator
    {
    private:
        static std::chrono::seconds default_sleep_duration;

    private:
        reader_container _readers;
        std::chrono::seconds _sleep;

        void idle();

    public:
        idle_evaluator(const reader_container& readers,
            const std::chrono::seconds& sleep_for = default_sleep_duration);

        cmmn::expected<idle_results, tracer_error> run();
    };

}