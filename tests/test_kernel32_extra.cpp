// Anyxbox kernel32 startup basics: last error, TLS (per thread), process heap, time, small stubs.
#include <cstdio>
#include <cstring>
#include <thread>
#include "runtime/kernel32.h"
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
#define MS __attribute__((ms_abi))
using namespace anyxbox;
static ImportRegistry reg;
template <class T> static T K(const char* n) { return (T)(uintptr_t)reg.resolve("kernel32.dll", n, 0, false); }
int main() {
  Kernel32Context ctx; CHECK(kernel32_register(reg, ctx));
  K<void (MS *)(uint32_t)>("SetLastError")(5); CHECK(K<uint32_t (MS *)()>("GetLastError")() == 5);
  uint32_t err_in_thread = 99;
  std::thread([&] { err_in_thread = K<uint32_t (MS *)()>("GetLastError")(); }).join();
  CHECK(err_in_thread == 0);                                    // last error is per thread
  auto alloc = K<uint32_t (MS *)()>("TlsAlloc"); auto set = K<uint32_t (MS *)(uint32_t, uint64_t)>("TlsSetValue");
  auto get = K<uint64_t (MS *)(uint32_t)>("TlsGetValue"); auto fr = K<uint32_t (MS *)(uint32_t)>("TlsFree");
  uint32_t a = alloc(), b = alloc(); CHECK(a != 0xFFFFFFFFu && b != 0xFFFFFFFFu && a != b);
  CHECK(set(a, 0x1234) && get(a) == 0x1234 && get(b) == 0);
  uint64_t other = 1; std::thread([&] { other = get(a); }).join(); CHECK(other == 0);   // TLS values are per thread
  CHECK(fr(a) && !fr(a) && !set(a, 1));
  CHECK(alloc() == a);                                          // freed slot is reused, zeroed
  CHECK(get(a) == 0 && get(999) == 0 && !set(999, 1));
  std::vector<uint32_t> held; for (int i = 0; i < 100; i++) { uint32_t s = alloc(); if (s == 0xFFFFFFFFu) break; held.push_back(s); }
  CHECK(held.size() < 100);                                     // finite slot table
  auto heap = K<uint64_t (MS *)()>("GetProcessHeap")(); CHECK(heap);
  auto ha = K<void* (MS *)(uint64_t, uint32_t, uint64_t)>("HeapAlloc"); auto hf = K<uint32_t (MS *)(uint64_t, uint32_t, void*)>("HeapFree");
  uint8_t* p = (uint8_t*)ha(heap, 8, 64); CHECK(p); for (int i = 0; i < 64; i++) CHECK(p[i] == 0);
  std::memset(p, 7, 64); CHECK(hf(heap, 0, p) == 1 && hf(heap, 0, p) == 0);   // double free is refused
  int local = 0; CHECK(hf(heap, 0, &local) == 0);                                // foreign pointers are refused
  CHECK(ha(heap, 0, ~0ull) == nullptr && ha(0x1, 0, 8) == nullptr);
  CHECK(hf(heap, 0, nullptr) == 1);
  uint64_t ft = 0; K<void (MS *)(uint64_t*)>("GetSystemTimeAsFileTime")(&ft); CHECK(ft > 132000000000000000ull);   // after year 2019
  uint32_t t1 = K<uint32_t (MS *)()>("GetTickCount")(); K<void (MS *)(uint32_t)>("Sleep")(20); uint32_t t2 = K<uint32_t (MS *)()>("GetTickCount")();
  CHECK(t2 - t1 >= 15 && t2 - t1 < 2000);
  CHECK(K<uint32_t (MS *)()>("IsDebuggerPresent")() == 0 && K<uint32_t (MS *)()>("GetCurrentProcessId")() != 0 && K<uint32_t (MS *)()>("GetCurrentThreadId")() != 0);
  CHECK(K<uint64_t (MS *)(uint64_t)>("SetUnhandledExceptionFilter")(1) == 0);
  CHECK(std::strlen(K<const char* (MS *)()>("GetCommandLineA")()) > 0);
  std::puts("test_kernel32_extra ok");
  return 0;
}
