// signaler.cpp

#include "signaler.hpp"

using namespace tep;


signaler::signaler(bool initial_state) :
    _open(initial_state),
    _m(),
    _cv()
{}

void signaler::post()
{
    {
        std::lock_guard lock(_m);
        if (!_open)
            _open = true;
    }
    _cv.notify_one();
}

void signaler::wait()
{
    std::unique_lock lock(_m);
    _cv.wait(lock, [this] { return _open == true; });
    _open = false;
}

void signaler::wait_for(const std::chrono::milliseconds& ms)
{
    std::unique_lock lock(_m);
    _cv.wait_for(lock, ms, [this] { return _open == true; });
    _open = false;
}

void signaler::wait_until(const std::chrono::steady_clock::time_point& tp)
{
    std::unique_lock lock(_m);
    _cv.wait_until(lock, tp, [this] { return _open == true; });
    _open = false;
}
