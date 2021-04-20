// signaler.hpp

#pragma once

#include <mutex>
#include <condition_variable>

namespace tep
{
    class signaler
    {
    private:
        bool _open;
        std::mutex _m;
        std::condition_variable _cv;

    public:
        signaler(bool initial_state = true);

        void post();
        void wait();
        void wait_for(const std::chrono::milliseconds& ms);
        void wait_until(const std::chrono::high_resolution_clock::time_point& tp);
    };
}
