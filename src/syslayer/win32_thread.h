// Anyxbox CreateThread-style wrapper and critical sections. GPL-2.0.
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include "syslayer/win32_sync.h"
namespace anyxbox {

// Signaled when the thread function has returned. Joins on destruction.
class ThreadObject : public KernelObject {
 public:
  explicit ThreadObject(std::function<uint32_t()> fn);
  ~ThreadObject() override;
  uint32_t exit_code() const { return exit_code_; }
  bool finished() const;
 protected:
  bool signaled_locked() override { return done_; }
  void consume_locked() override {}
 private:
  bool done_ = false;
  std::atomic<uint32_t> exit_code_{259};  // STILL_ACTIVE
  std::thread th_;
};

// Returns a handle, or 0 on failure.
uint32_t CreateThread(HandleTable& t, std::function<uint32_t()> fn);
// STILL_ACTIVE (259) while running; false if handle is not a thread.
bool GetExitCodeThread(const HandleTable& t, uint32_t h, uint32_t* code);

// Recursive, like Win32 CRITICAL_SECTION.
class CriticalSection {
 public:
  void enter() { m_.lock(); }
  bool try_enter() { return m_.try_lock(); }
  void leave() { m_.unlock(); }
 private:
  std::recursive_mutex m_;
};
}
