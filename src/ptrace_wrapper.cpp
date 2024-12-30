// ptrace_wrapper.cpp

#include "ptrace_wrapper.hpp"

#include <cstdarg>
#include <stdexcept>
#include <unistd.h>

using namespace tep;

ptrace_wrapper ptrace_wrapper::instance;

enum class ptrace_wrapper::request : uint32_t {
  ptrace,
  fork,
  finish,
};

ptrace_wrapper::ptrace_wrapper() : _data(), _global_mx(), _calling_thread() {
  if (sem_init(&_req_sem, 0, 0))
    throw std::runtime_error("unable to initialize request semaphore");
  if (sem_init(&_res_sem, 0, 0)) {
    sem_destroy(&_req_sem);
    throw std::runtime_error("unable to initialize result semaphore");
  }
  _calling_thread = std::thread(&ptrace_wrapper::thread_work, this);
}

ptrace_wrapper::~ptrace_wrapper() noexcept {
  if (_calling_thread.joinable()) {
    std::lock_guard lock(_global_mx);
    _data.req = request::finish;
    sem_post(&_req_sem);
    _calling_thread.join();
  }
  sem_destroy(&_req_sem);
  sem_destroy(&_res_sem);
}

long ptrace_wrapper::ptrace(int &error, ptrace_wrapper::ptrace_req req,
                            pid_t pid, ...) noexcept {
  std::lock_guard lock(_global_mx);
  va_list va;
  va_start(va, pid);
  _data.req = request::ptrace;
  _data.ptrace.req = req;
  _data.ptrace.pid = pid;
  _data.ptrace.addr = va_arg(va, void *);
  _data.ptrace.data = va_arg(va, void *);
  va_end(va);
  sem_post(&_req_sem);
  sem_wait(&_res_sem);
  error = _data.error;
  return _data.ptrace.result;
}

pid_t ptrace_wrapper::fork(int &error, callback_t *callback,
                           callback_args_t args) noexcept {
  std::lock_guard lock(_global_mx);
  _data.req = request::fork;
  _data.fork.callback = callback;
  _data.fork.args = args;
  sem_post(&_req_sem);
  sem_wait(&_res_sem);
  error = _data.error;
  return _data.fork.result;
}

void ptrace_wrapper::thread_work() noexcept {
  while (true) {
    sem_wait(&_req_sem);
    switch (_data.req) {
    case request::fork:
      errno = 0;
      _data.fork.result = ::fork();
      if (_data.fork.result == 0) {
        _data.fork.callback(_data.fork.args);
        _exit(1);
      }
      _data.error = errno;
      break;
    case request::ptrace:
      errno = 0;
      _data.ptrace.result = ::ptrace(_data.ptrace.req, _data.ptrace.pid,
                                     _data.ptrace.addr, _data.ptrace.data);
      _data.error = errno;
      break;
    case request::finish:
      return;
    };
    sem_post(&_res_sem);
  }
}
