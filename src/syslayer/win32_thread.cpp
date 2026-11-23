#include "syslayer/win32_thread.h"
namespace anyxbox {
ThreadObject::ThreadObject(std::function<uint32_t()> fn) {
  th_ = std::thread([this, f = std::move(fn)] {
    uint32_t code = 0;
    try { code = f(); } catch (...) { code = 0xC0000005; }
    exit_code_ = code;
    { std::lock_guard<std::mutex> l(m_); done_ = true; }
    cv_.notify_all();
  });
}
ThreadObject::~ThreadObject() { if (th_.joinable()) th_.join(); }
bool ThreadObject::finished() const { return const_cast<ThreadObject*>(this)->try_acquire(); }
uint32_t CreateThread(HandleTable& t, std::function<uint32_t()> fn) {
  if (!fn) return 0;
  try { return t.add(std::make_shared<ThreadObject>(std::move(fn))); } catch (...) { return 0; }
}
bool GetExitCodeThread(const HandleTable& t, uint32_t h, uint32_t* code) {
  auto o = std::dynamic_pointer_cast<ThreadObject>(t.get(h));
  if (!o) return false;
  if (code) *code = o->exit_code();
  return true;
}
}
