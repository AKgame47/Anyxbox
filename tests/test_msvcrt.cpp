#include <cstdio>
#include <cstring>
#include "pe_builder.h"
#include "runtime/msvcrt.h"
#include "runtime/run_pe.h"
using namespace anyxbox;
#define MS __attribute__((ms_abi))
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
static ImportRegistry reg;
template <class F> static F fn(const char* n) { return (F)(uintptr_t)reg.resolve("msvcrt.dll", n, 0, false); }
static void emit(std::vector<uint8_t>& c, std::initializer_list<uint8_t> l){ c.insert(c.end(), l.begin(), l.end()); }
static void emit32(std::vector<uint8_t>& c, uint32_t v){ for(int i=0;i<4;i++) c.push_back((v>>(8*i))&0xff); }
static void rip(std::vector<uint8_t>& c, uint32_t target){ emit32(c, target - (0x1000 + (uint32_t)c.size() + 4)); }
int main() {
  MsvcrtContext ctx; std::string out; ctx.out = &out;
  CHECK(msvcrt_register(reg, ctx));
  CHECK(reg.resolve("UCRTBASE.dll", "malloc", 0, false) == reg.resolve("msvcrt.dll", "malloc", 0, false));
  char a[8] = "abcdefg", b[8] = {0};
  CHECK(fn<MS void*(*)(void*, const void*, uint64_t)>("memcpy")(b, a, 8) == b && !std::strcmp(b, "abcdefg"));
  fn<MS void*(*)(void*, int, uint64_t)>("memset")(b, 'z', 3); CHECK(!std::strcmp(b, "zzzdefg"));
  fn<MS void*(*)(void*, const void*, uint64_t)>("memmove")(a + 1, a, 4); CHECK(!std::strncmp(a, "aabcdfg", 7));
  CHECK(fn<MS int(*)(const void*, const void*, uint64_t)>("memcmp")("ab", "ac", 2) < 0);
  CHECK(fn<MS int(*)(const void*, const void*, uint64_t)>("memcmp")(nullptr, nullptr, 0) == 0);
  CHECK(fn<MS uint64_t(*)(const char*)>("strlen")("hello") == 5);
  CHECK(fn<MS int(*)(const char*, const char*)>("strcmp")("a", "a") == 0 && fn<MS int(*)(const char*, const char*, uint64_t)>("strncmp")("abx", "aby", 2) == 0);
  CHECK(fn<MS int(*)(int)>("abs")(-5) == 5);
  // heap
  auto mal = fn<MS void*(*)(uint64_t)>("malloc"); auto fre = fn<MS void(*)(void*)>("free");
  auto cal = fn<MS void*(*)(uint64_t, uint64_t)>("calloc"); auto rea = fn<MS void*(*)(void*, uint64_t)>("realloc");
  char* p = (char*)mal(16); CHECK(p && ctx.heap_used == 16);
  std::strcpy(p, "grow me");
  p = (char*)rea(p, 64); CHECK(p && !std::strcmp(p, "grow me") && ctx.heap_used == 64);
  fre(p); fre(p); CHECK(ctx.heap_used == 0);                     // double free ignored
  int dummy; fre(&dummy); fre(nullptr);                          // foreign pointer ignored
  char* z = (char*)cal(4, 4); CHECK(z && z[15] == 0); fre(z);
  CHECK(cal(~0ull, 2) == nullptr && mal(~0ull) == nullptr && rea(&dummy, 8) == nullptr);
  CHECK(ctx.heap.empty());
  ctx.heap_limit = 100; CHECK(mal(101) == nullptr); void* q = mal(100); CHECK(q && mal(1) == nullptr); fre(q); ctx.heap_limit = 1 << 30;
  // output
  out.clear();
  fn<MS int(*)(const char*)>("puts")("hi"); fn<MS int(*)(int)>("putchar")('!');
  CHECK(out == "hi\n!");
  out.clear();
  auto pf = fn<MS int(*)(const char*, ...)>("printf");
  int n = pf("%d|%5d|%-5d|%05d|%u|%x|%X|%c|%s|%%|%lld|%s", -42, 7, 7, 42, 3000000000u, 255, 255, 'Q', "str", 5000000000ll, (const char*)nullptr);
  CHECK(out == "-42|    7|7    |00042|3000000000|ff|FF|Q|str|%|5000000000|(null)");
  CHECK(n == (int)out.size());
  out.clear(); pf("%d %d %d %d %d %d", 1, 2, 3, 4, 5, 6); CHECK(out == "1 2 3 4 5 6");   // stack-passed varargs
  out.clear(); pf("100%"); CHECK(out == "100");  // trailing lone % emits nothing
  // end to end: program calls printf("%d-%s\n", 42, "ok") and returns its result
  { std::vector<uint8_t> c;
    emit(c, {0x48,0x83,0xEC,0x38});                       // sub rsp,0x38
    emit(c, {0x48,0x8D,0x0D}); rip(c, 0x1100);            // lea rcx,[fmt]
    emit(c, {0xBA}); emit32(c, 42);                       // mov edx,42
    emit(c, {0x4C,0x8D,0x05}); rip(c, 0x1120);            // lea r8,[str]
    emit(c, {0xFF,0x15}); rip(c, 0x1260);                 // call [printf]
    emit(c, {0x48,0x83,0xC4,0x38, 0xC3});                 // add rsp,0x38 ; ret
    auto img = pebuild::make(c, {"printf"}, "msvcrt.dll");
    std::memcpy(&img[0x200 + 0x100], "%d-%s\n", 7);
    std::memcpy(&img[0x200 + 0x120], "ok", 3);
    auto r = run_pe(img.data(), img.size(), true);
    CHECK(r.ran && r.output == "42-ok\n" && r.exit_code == 6); }
  std::puts("ok"); return 0;
}
