// Anyxbox: the fake thread environment block behind gs (what a C runtime startup reads first).
#include <cstdio>
#include <thread>
#include "runtime/teb.h"
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
using namespace anyxbox;
static uint64_t rd(int off) { uint64_t v; __asm__ volatile("movq %%gs:(%1), %0" : "=r"(v) : "r"((uint64_t)off)); return v; }
static int check_thread(uint64_t base) {
  if (!teb_install(base)) return 10;
  uint64_t self = rd(0x30); if (self != (uint64_t)(uintptr_t)teb_current()) return 11;
  uint64_t sb = rd(0x08), sl = rd(0x10); int local = 0; uint64_t a = (uint64_t)(uintptr_t)&local;
  if (!(sl < a && a < sb)) return 12;                                   // a stack variable lies between limit and base
  uint64_t peb = rd(0x60); if (!peb) return 13;
  uint64_t img; __builtin_memcpy(&img, (void*)(uintptr_t)(peb + 0x10), 8); if (img != base) return 14;
  uint32_t major; __builtin_memcpy(&major, (void*)(uintptr_t)(peb + 0x118), 4); if (major != 10) return 15;
  if (rd(0x58) == 0) return 16;
  teb_restore(); if (teb_current()) return 17;
  return 0;
}
int main() {
  CHECK(check_thread(0x140000000ull) == 0);
  int r = -1; std::thread([&] { r = check_thread(0x180000000ull); }).join(); CHECK(r == 0);   // every thread gets its own block
  CHECK(teb_current() == nullptr);
  std::puts("test_teb ok");
  return 0;
}
