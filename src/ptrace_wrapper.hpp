// ptrace_wrapper.hpp

#pragma once

#include <mutex>
#include <thread>

#include <semaphore.h>
#include <sys/ptrace.h>

namespace tep
{

    class ptrace_wrapper
    {
    public:
        static ptrace_wrapper instance;

    private:
        enum class request;

        struct request_data
        {
            request req;
            union
            {
                struct
                {
                    long result;
                    __ptrace_request req;
                    pid_t pid;
                    void* addr;
                    void* data;
                } ptrace;
                struct
                {
                    pid_t result;
                    void (*callback)(char* const []);
                    char** arg;
                } fork;
            };
            int error;
        };

        request_data _data;
        std::mutex _global_mx;
        std::thread _calling_thread;
        sem_t _req_sem;
        sem_t _res_sem;

        void thread_work() noexcept;

        ptrace_wrapper();
        ~ptrace_wrapper() noexcept;

    public:
        long ptrace(int& error, __ptrace_request req, pid_t pid, ...) noexcept;
        pid_t fork(int& error, void(*callback)(char* const []), char** arg) noexcept;

    };

}
