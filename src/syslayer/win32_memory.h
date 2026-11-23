// Anyxbox VirtualAlloc-style region tracker (host-backed, aligned). GPL-2.0.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
namespace anyxbox {
class VirtualMemory {
 public:
  ~VirtualMemory();
  // Page-aligned (4096) zeroed allocation. Returns nullptr on size 0 or failure.
  void* alloc(size_t size);
  // Frees only a pointer returned by alloc (exact base). Returns false otherwise.
  bool free(void* p);
  // Size of the region containing p, or 0.
  size_t region_size(const void* p) const;
  size_t bytes_committed() const;
 private:
  mutable std::mutex m_;
  std::map<uintptr_t, size_t> r_;
};
}
