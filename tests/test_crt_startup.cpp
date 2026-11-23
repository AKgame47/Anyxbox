// Anyxbox: the C runtime startup set a default MinGW program imports (msvcrt.dll + a few kernel32 functions), called as host functions.
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <thread>
#include <vector>
#include "runtime/kernel32.h"
#include "runtime/msvcrt.h"
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
#define MS __attribute__((ms_abi))
using namespace anyxbox;
static ImportRegistry reg;
template <class T> static T M(const char* n) { T f = (T)(uintptr_t)reg.resolve("msvcrt.dll", n, 0, false); return f; }
template <class T> static T K(const char* n) { return (T)(uintptr_t)reg.resolve("kernel32.dll", n, 0, false); }
static std::vector<int> order;
static MS void t1() { order.push_back(1); }
static MS void t2() { order.push_back(2); }
static MS void t3() { order.push_back(3); }
static std::jmp_buf jb; static uint32_t hook_code = 0;
static void hook(uint32_t c) { hook_code = c; std::longjmp(jb, 1); }
static MS int call_vf(void* f, const char* fmt, ...) { __builtin_ms_va_list ap; __builtin_ms_va_start(ap, fmt); int r = M<int (MS *)(void*, const char*, __builtin_ms_va_list)>("vfprintf")(f, fmt, ap); __builtin_ms_va_end(ap); return r; }
int main() {
  std::string out; MsvcrtContext crt; crt.out = &out; crt.exit_fn = hook; Kernel32Context k; k.out = &out;
  CHECK(msvcrt_register(reg, crt) && kernel32_register(reg, k));
  for (const char* d : {"msvcrt.dll", "ucrtbase.dll"}) CHECK(reg.resolve(d, "__getmainargs", 0, false));
  // every import CI reported missing for hello_crt.exe (batch 27 log, run #15) now resolves
  for (const char* n : {"GetStartupInfoA", "IsDBCSLeadByteEx", "MultiByteToWideChar", "VirtualProtect", "VirtualQuery", "WideCharToMultiByte"}) CHECK(reg.resolve("kernel32.dll", n, 0, false));
  for (const char* n : {"__C_specific_handler", "___lc_codepage_func", "___mb_cur_max_func", "__getmainargs", "__initenv", "__iob_func", "__lconv_init", "__set_app_type",
       "__setusermatherr", "_acmdln", "_amsg_exit", "_cexit", "_commode", "_errno", "_fmode", "_initterm", "_lock", "_onexit", "_unlock", "abort", "exit", "fprintf", "fputc",
       "fwrite", "localeconv", "signal", "strerror", "vfprintf", "wcslen"}) { if (!reg.resolve("msvcrt.dll", n, 0, false)) std::printf("missing %s\n", n); CHECK(reg.resolve("msvcrt.dll", n, 0, false)); }
  int argc = 0; char** argv = nullptr; char** env = nullptr;
  CHECK(M<int (MS *)(int*, char***, char***, int, void*)>("__getmainargs")(&argc, &argv, &env, 0, nullptr) == 0);
  CHECK(argc == 1 && argv && argv[0] && argv[1] == nullptr && env && env[0] == nullptr);
  CHECK(M<int (MS *)(int*, char***, char***, int, void*)>("__getmainargs")(nullptr, &argv, &env, 0, nullptr) == -1);
  // data imports are addresses of variables
  CHECK(*(int*)(uintptr_t)reg.resolve("msvcrt.dll", "_fmode", 0, false) == 0 && *(int*)(uintptr_t)reg.resolve("msvcrt.dll", "_commode", 0, false) == 0);
  CHECK(**(char**)(uintptr_t)reg.resolve("msvcrt.dll", "_acmdln", 0, false) == 'a' && *(char***)(uintptr_t)reg.resolve("msvcrt.dll", "__initenv", 0, false) != nullptr);
  // _initterm skips null entries and runs the others in order
  { void (MS *tbl[4])() = {t1, nullptr, t2, t3}; M<void (MS *)(void (MS **)(), void (MS **)())>("_initterm")(tbl, tbl + 4); CHECK((order == std::vector<int>{1, 2, 3})); order.clear(); }
  // _onexit handlers run in reverse order on _cexit, once
  { auto oe = M<void* (MS *)(void*)>("_onexit"); CHECK(oe((void*)&t1) && oe((void*)&t2) && oe(nullptr) == nullptr);
    M<void (MS *)()>("_cexit")(); CHECK((order == std::vector<int>{2, 1})); M<void (MS *)()>("_cexit")(); CHECK(order.size() == 2); order.clear(); }
  // exit runs handlers, then calls the hook and never returns to the caller
  { M<void* (MS *)(void*)>("_onexit")((void*)&t3);
    if (setjmp(jb) == 0) { M<void (MS *)(int)>("exit")(7); std::puts("exit returned"); return 1; }
    CHECK(hook_code == 7 && crt.exited && crt.exit_code == 7 && (order == std::vector<int>{3})); order.clear();
    if (setjmp(jb) == 0) { M<void (MS *)()>("abort")(); return 1; } CHECK(hook_code == 3);
    if (setjmp(jb) == 0) { M<void (MS *)(int)>("_amsg_exit")(1); return 1; } CHECK(hook_code == 255); }
  // stdio on the standard streams
  void* iob = M<void* (MS *)()>("__iob_func")(); CHECK(iob);
  uint8_t* io = (uint8_t*)iob; void* in = io, *so = io + 48, *se = io + 96;
  auto fw = M<uint64_t (MS *)(const void*, uint64_t, uint64_t, void*)>("fwrite"); auto fc = M<int (MS *)(int, void*)>("fputc");
  CHECK(fw("abc", 1, 3, so) == 3 && fc('d', se) == 'd' && fw("x", 1, 1, in) == 0 && fc('z', in) == -1 && fw("x", 1, 1, (void*)&out) == 0);
  CHECK(fw("x", ~0ull, ~0ull, so) == 0 && out == "abcd");
  out.clear();
  auto fp = M<int (MS *)(void*, const char*, ...)>("fprintf");
  CHECK(fp(se, "[%5.2f|%-5d|%+d|%.3s|%*d|%lld|%I64u|%ld|%x|%c|%%|%05d|%s]", 3.14159, 42, 7, "abcdef", 4, 9, -5ll, 18446744073709551615ull, -1, 255u, 'q', 42, (const char*)nullptr) > 0);
  CHECK(out == "[ 3.14|42   |+7|abc|   9|-5|18446744073709551615|-1|ff|q|%|00042|(null)]");
  out.clear(); CHECK(call_vf(so, "%d-%s-%g", 12, "v", 0.5) == 8 && out == "12-v-0.5");
  out.clear(); CHECK(fp(in, "no") == -1 && fp(so, nullptr) == -1 && out.empty());
  out.clear(); CHECK(M<int (MS *)(const char*, ...)>("printf")("%u %5.1f\n", 3u, 2.25) == 8 && out == "3   2.2\n");   // 2.25 rounds to 2.2 (banker's in host printf)
  // small functions
  int* e1 = M<int* (MS *)()>("_errno")(); *e1 = 5; int other = -1; std::thread([&] { other = *M<int* (MS *)()>("_errno")(); }).join(); CHECK(*e1 == 5 && other == 0);
  CHECK(std::strlen(M<const char* (MS *)(int)>("strerror")(2)) > 0);
  const char** lc = (const char**)M<void* (MS *)()>("localeconv")(); CHECK(lc[0][0] == '.' && lc[0][1] == 0 && lc[1][0] == 0);
  const uint16_t w[] = {'h', 'i', 0}; CHECK(M<uint64_t (MS *)(const uint16_t*)>("wcslen")(w) == 2);
  CHECK(M<int (MS *)()>("___mb_cur_max_func")() == 1 && M<int (MS *)()>("__lconv_init")() == 0 && M<int (MS *)(void*, void*, void*, void*)>("__C_specific_handler")(0, 0, 0, 0) == 1);
  M<void (MS *)(int)>("_lock")(3); M<void (MS *)(int)>("_lock")(3); M<void (MS *)(int)>("_unlock")(3); M<void (MS *)(int)>("_unlock")(3); M<void (MS *)(int)>("_lock")(999);
  CHECK(M<void* (MS *)(int, void*)>("signal")(2, nullptr) == nullptr);
  // kernel32
  uint8_t si[104]; std::memset(si, 0xAA, sizeof si); K<void (MS *)(uint8_t*)>("GetStartupInfoA")(si); { uint32_t cb; std::memcpy(&cb, si, 4); CHECK(cb == 104 && si[4] == 0 && si[103] == 0); }
  CHECK(K<uint32_t (MS *)(uint32_t, uint8_t)>("IsDBCSLeadByteEx")(0, 0x81) == 0);
  auto mb = K<int32_t (MS *)(uint32_t, uint32_t, const uint8_t*, int32_t, uint16_t*, int32_t)>("MultiByteToWideChar");
  uint16_t wb[8] = {}; CHECK(mb(0, 0, (const uint8_t*)"abc", -1, nullptr, 0) == 4 && mb(0, 0, (const uint8_t*)"abc", -1, wb, 8) == 4 && wb[2] == 'c' && wb[3] == 0);
  CHECK(mb(0, 0, (const uint8_t*)"abc", -1, wb, 2) == 0 && K<uint32_t (MS *)()>("GetLastError")() == 122 && mb(0, 0, nullptr, 1, wb, 8) == 0);
  auto wm = K<int32_t (MS *)(uint32_t, uint32_t, const uint16_t*, int32_t, uint8_t*, int32_t, const char*, uint32_t*)>("WideCharToMultiByte");
  const uint16_t wide[] = {'a', 0x20AC, 0}; uint8_t nb[8] = {}; uint32_t used = 9;
  CHECK(wm(0, 0, wide, -1, nullptr, 0, nullptr, nullptr) == 3 && wm(0, 0, wide, -1, nb, 8, nullptr, &used) == 3 && nb[0] == 'a' && nb[1] == '?' && used == 1);
  // page protection inside the image range only
  size_t pg = 4096; uint8_t* mem = (uint8_t*)mmap(nullptr, 3 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); CHECK(mem != MAP_FAILED);
  k.image_base = (uint64_t)(uintptr_t)mem; k.image_size = 3 * pg;
  auto vq = K<uint64_t (MS *)(uint64_t, uint8_t*, uint64_t)>("VirtualQuery"); auto vp = K<uint32_t (MS *)(uint64_t, uint64_t, uint32_t, uint32_t*)>("VirtualProtect");
  uint8_t mbi[48]; CHECK(vq((uint64_t)(uintptr_t)mem + 5, mbi, 48) == 48); { uint64_t base; uint32_t st, pr; std::memcpy(&base, mbi, 8); std::memcpy(&st, mbi + 32, 4); std::memcpy(&pr, mbi + 36, 4); CHECK(base == (uint64_t)(uintptr_t)mem && st == 0x1000 && pr == 4); }
  CHECK(vq((uint64_t)(uintptr_t)mem, mbi, 40) == 0);
  uint32_t old = 0; CHECK(vp((uint64_t)(uintptr_t)mem + pg, pg, 2, &old) == 1 && old == 4);
  CHECK(vq((uint64_t)(uintptr_t)mem + pg, mbi, 48) == 48); { uint32_t pr; std::memcpy(&pr, mbi + 36, 4); CHECK(pr == 2); }
  CHECK(vp((uint64_t)(uintptr_t)mem + pg, pg, 4, &old) == 1 && old == 2);
  mem[pg] = 1;                                                             // writable again
  CHECK(vp((uint64_t)(uintptr_t)mem + 2 * pg, 2 * pg, 4, &old) == 0);       // runs past the image: refused
  int stack_var = 0; CHECK(vp((uint64_t)(uintptr_t)&stack_var, 4, 0x40, &old) == 0 && vq((uint64_t)(uintptr_t)&stack_var, mbi, 48) == 0);   // host memory is off limits
  CHECK(vp((uint64_t)(uintptr_t)mem, pg, 0x1234, &old) == 0 && vp((uint64_t)(uintptr_t)mem, 0, 4, &old) == 0);
  munmap(mem, 3 * pg);
  std::puts("test_crt_startup ok");
  return 0;
}
