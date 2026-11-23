#include "syslayer/win32_memory.h"
#include <cstdlib>
#include <cstring>
namespace anyxbox {
static const size_t kPage = 4096;
VirtualMemory::~VirtualMemory() { for (auto& kv : r_) std::free((void*)kv.first); }
void* VirtualMemory::alloc(size_t size) {
  if (size == 0 || size > (size_t(1) << 36)) return nullptr;
  size_t rounded = (size + kPage - 1) & ~(kPage - 1);
  void* p = std::aligned_alloc(kPage, rounded);
  if (!p) return nullptr;
  std::memset(p, 0, rounded);
  std::lock_guard<std::mutex> l(m_);
  r_[(uintptr_t)p] = rounded;
  return p;
}
bool VirtualMemory::free(void* p) {
  std::lock_guard<std::mutex> l(m_);
  auto it = r_.find((uintptr_t)p);
  if (it == r_.end()) return false;
  std::free(p); r_.erase(it); return true;
}
size_t VirtualMemory::region_size(const void* p) const {
  std::lock_guard<std::mutex> l(m_);
  uintptr_t a = (uintptr_t)p;
  auto it = r_.upper_bound(a);
  if (it == r_.begin()) return 0;
  --it;
  return a < it->first + it->second ? it->second : 0;
}
size_t VirtualMemory::bytes_committed() const {
  std::lock_guard<std::mutex> l(m_);
  size_t t = 0; for (auto& kv : r_) t += kv.second; return t;
}
}
