#include "syslayer/win32_sync.h"
#include <chrono>
namespace anyxbox {
bool KernelObject::try_acquire() {
  std::lock_guard<std::mutex> l(m_);
  if (!signaled_locked()) return false;
  consume_locked();
  return true;
}
uint32_t KernelObject::wait(uint32_t ms) {
  std::unique_lock<std::mutex> l(m_);
  if (ms == INFINITE_MS) cv_.wait(l, [&] { return signaled_locked(); });
  else if (!cv_.wait_for(l, std::chrono::milliseconds(ms), [&] { return signaled_locked(); })) return WAIT_TIMEOUT;
  consume_locked();
  return WAIT_OBJECT_0;
}
void Event::set() { { std::lock_guard<std::mutex> l(m_); set_ = true; } cv_.notify_all(); }
void Event::reset() { std::lock_guard<std::mutex> l(m_); set_ = false; }
bool Semaphore::release(int32_t n, int32_t* prev) {
  { std::lock_guard<std::mutex> l(m_);
    if (n <= 0 || count_ > max_ - n) return false;
    if (prev) *prev = count_;
    count_ += n; }
  cv_.notify_all();
  return true;
}
uint32_t HandleTable::add(std::shared_ptr<KernelObject> o) {
  std::lock_guard<std::mutex> l(m_);
  uint32_t h = next_; next_ += 4; t_[h] = std::move(o); return h;
}
std::shared_ptr<KernelObject> HandleTable::get(uint32_t h) const {
  std::lock_guard<std::mutex> l(m_);
  auto it = t_.find(h); return it == t_.end() ? nullptr : it->second;
}
bool HandleTable::close(uint32_t h) { std::lock_guard<std::mutex> l(m_); return t_.erase(h) > 0; }
size_t HandleTable::size() const { std::lock_guard<std::mutex> l(m_); return t_.size(); }
uint32_t WaitForSingleObject(const HandleTable& t, uint32_t h, uint32_t ms) {
  auto o = t.get(h);
  if (!o) return WAIT_FAILED;
  return o->wait(ms);
}
}
