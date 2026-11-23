// Anyxbox: the fake thread environment block behind gs (what a C runtime startup reads first).
#include <cstdio>
#include <thread>
#include "runtime/teb.h"
#include "runtime/kernel32.h"
#include <vector>
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
#define MS __attribute__((ms_abi))
static std::vector<int> events;
static bool callbacks_ok = true;
static uint32_t* tls() { return (uint32_t*)(uintptr_t)*(uint64_t*)(uintptr_t)rd(0x58); }
static MS void callback(void* base, uint32_t reason, void* reserved) {
  callbacks_ok &= base == (void*)0x140000000ull && !reserved && anyxbox::teb_current();
  if (reason == 2) { callbacks_ok &= tls()[0] == 41 && tls()[1] == 0; tls()[0] = 42; }
  if (reason == 3) callbacks_ok &= tls()[0] == 43;
  events.push_back((int)reason);
}
static MS void callback2(void*, uint32_t reason, void*) { events.push_back((int)reason + 10); }
static MS uint32_t start(void*) { callbacks_ok &= tls()[0] == 42 && tls()[1] == 0; tls()[0] = 43; events.push_back(20); return 7; }
int main() {
  CHECK(check_thread(0x140000000ull) == 0);
  int r = -1; std::thread([&] { r = check_thread(0x180000000ull); }).join(); CHECK(r == 0);   // every thread gets its own block
  CHECK(teb_current() == nullptr);
  ImportRegistry reg; Kernel32Context ctx; ctx.image_base = 0x140000000ull;
  ctx.tls_callbacks = {(uint64_t)(uintptr_t)&callback, (uint64_t)(uintptr_t)&callback2};
  CHECK(kernel32_register(reg, ctx));
  uint32_t raw = 41; teb_set_tls_template((const uint8_t*)&raw, 4, 4);
  CHECK(teb_install(ctx.image_base)); tls()[0] = 99; // worker must receive a fresh template, not this thread's modified block
  auto create = (uint64_t (MS *)(void*, uint64_t, uint32_t (MS *)(void*), void*, uint32_t, uint32_t*))reg.resolve("kernel32.dll", "CreateThread", 0, false);
  for (int i = 0; i < 2; i++) {
    events.clear(); uint64_t h = create(nullptr, 0, start, nullptr, 0, nullptr); CHECK(h);
    CHECK(WaitForSingleObject(ctx.handles, (uint32_t)h, 5000) == WAIT_OBJECT_0);
    uint32_t code = 0; CHECK(GetExitCodeThread(ctx.handles, (uint32_t)h, &code) && code == 7);
    CHECK(callbacks_ok && events == std::vector<int>({2, 12, 20, 3, 13}));
    CHECK(tls()[0] == 99); CHECK(ctx.handles.close((uint32_t)h));
  }
  teb_restore(); teb_set_tls_template(nullptr, 0, 0);
  std::puts("test_teb ok");
  return 0;
}
