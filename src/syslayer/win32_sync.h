// Anyxbox Win32-style kernel objects: handle table, events, semaphores, waits. GPL-2.0.
#pragma once
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
namespace anyxbox {

enum : uint32_t { WAIT_OBJECT_0 = 0, WAIT_TIMEOUT = 0x102, WAIT_FAILED = 0xFFFFFFFF, INFINITE_MS = 0xFFFFFFFF };

class KernelObject {
 public:
  virtual ~KernelObject() = default;
  // Called with lock held by wait; returns true and consumes the signal if acquired.
  bool try_acquire();
  void wait_cv_notify() { cv_.notify_all(); }
  uint32_t wait(uint32_t ms);
 protected:
  virtual bool signaled_locked() = 0;
  virtual void consume_locked() = 0;
  std::mutex m_;
  std::condition_variable cv_;
};

class Event : public KernelObject {
 public:
  Event(bool manual_reset, bool initial) : manual_(manual_reset), set_(initial) {}
  void set();
  void reset();
 protected:
  bool signaled_locked() override { return set_; }
  void consume_locked() override { if (!manual_) set_ = false; }
 private:
  bool manual_, set_;
};

class Semaphore : public KernelObject {
 public:
  Semaphore(int32_t initial, int32_t max) : count_(initial), max_(max) {}
  // Returns false (no change) if it would exceed max. prev gets the old count.
  bool release(int32_t n, int32_t* prev);
 protected:
  bool signaled_locked() override { return count_ > 0; }
  void consume_locked() override { count_--; }
 private:
  int32_t count_, max_;
};

// Handles are multiples of 4, never 0 or 0xFFFFFFFF, like Win32.
class HandleTable {
 public:
  uint32_t add(std::shared_ptr<KernelObject> o);
  std::shared_ptr<KernelObject> get(uint32_t h) const;
  bool close(uint32_t h);
  size_t size() const;
 private:
  mutable std::mutex m_;
  std::map<uint32_t, std::shared_ptr<KernelObject>> t_;
  uint32_t next_ = 4;
};

uint32_t WaitForSingleObject(const HandleTable& t, uint32_t h, uint32_t ms);
}
