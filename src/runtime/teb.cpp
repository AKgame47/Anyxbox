#include "runtime/teb.h"
#include <cstring>
#if defined(__linux__) && defined(__x86_64__)
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
namespace anyxbox {
namespace {
alignas(4096) uint8_t g_peb[4096];
alignas(16) uint8_t g_params[0x440];             // RTL_USER_PROCESS_PARAMETERS: present but empty
struct Teb { alignas(4096) uint8_t b[4096]; uint64_t tls_slots[64]; };
thread_local Teb t_teb;
thread_local bool t_installed = false;
thread_local uint64_t t_old_gs = 0;
void put64(uint8_t* p, size_t o, uint64_t v) { std::memcpy(p + o, &v, 8); }
void put32(uint8_t* p, size_t o, uint32_t v) { std::memcpy(p + o, &v, 4); }
}
bool teb_install(uint64_t image_base) {
  put64(g_peb, 0x10, image_base); put64(g_peb, 0x20, (uint64_t)(uintptr_t)g_params); put64(g_peb, 0x30, 0x9000);   // image base, parameters, process heap
  put32(g_peb, 0xB8, 1); put32(g_peb, 0x118, 10); put32(g_peb, 0x11C, 0); put32(g_peb, 0x120, 19045); put32(g_peb, 0x124, 2);
  uint8_t* t = t_teb.b;
  uint64_t hi = 0, lo = 0;
  pthread_attr_t a;
  if (pthread_getattr_np(pthread_self(), &a) == 0) { void* addr; size_t sz; if (pthread_attr_getstack(&a, &addr, &sz) == 0) { lo = (uint64_t)(uintptr_t)addr; hi = lo + sz; } pthread_attr_destroy(&a); }
  put64(t, 0x08, hi); put64(t, 0x10, lo);                                  // StackBase (high), StackLimit (low)
  put64(t, 0x30, (uint64_t)(uintptr_t)t);                                  // Self
  put32(t, 0x40, 4242); put32(t, 0x48, (uint32_t)syscall(SYS_gettid));     // ClientId
  put64(t, 0x58, (uint64_t)(uintptr_t)t_teb.tls_slots);                    // ThreadLocalStoragePointer
  put64(t, 0x60, (uint64_t)(uintptr_t)g_peb);                              // PEB
  if (!t_installed) {
    unsigned long cur = 0;
    t_old_gs = syscall(SYS_arch_prctl, 0x1004 /* ARCH_GET_GS */, &cur) == 0 ? cur : 0;
  }
  if (syscall(SYS_arch_prctl, 0x1001 /* ARCH_SET_GS */, (unsigned long)(uintptr_t)t) != 0) return false;
  t_installed = true;
  return true;
}
void teb_restore() {
  if (!t_installed) return;
  syscall(SYS_arch_prctl, 0x1001, (unsigned long)t_old_gs);
  t_installed = false;
}
const void* teb_current() { return t_installed ? t_teb.b : nullptr; }
}
#else
namespace anyxbox {
bool teb_install(uint64_t) { return false; }
void teb_restore() {}
const void* teb_current() { return nullptr; }
}
#endif
