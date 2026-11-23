#include <cstdio>
#include <cstring>
#include <filesystem>
#include "pe_builder.h"
#include "runtime/kernel32.h"
#include "runtime/run_pe.h"
using namespace anyxbox;
#define MS __attribute__((ms_abi))
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
static ImportRegistry reg;
template <class F> static F fn(const char* n) { return (F)(uintptr_t)reg.resolve("KERNEL32.DLL", n, 0, false); }
static void emit(std::vector<uint8_t>& c, std::initializer_list<uint8_t> l){ c.insert(c.end(), l.begin(), l.end()); }
static void emit32(std::vector<uint8_t>& c, uint32_t v){ for(int i=0;i<4;i++) c.push_back((v>>(8*i))&0xff); }
static void rip(std::vector<uint8_t>& c, uint32_t target){ emit32(c, target - (0x1000 + (uint32_t)c.size() + 4)); }

int main() {
  Kernel32Context ctx;
  std::string root = (std::filesystem::temp_directory_path() / "anyxbox_k32").string();
  std::filesystem::remove_all(root);
  ctx.folder = std::make_unique<MappedFolder>(root);
  ctx.image_base = 0x1234000;
  std::string out; ctx.out = &out;
  CHECK(kernel32_register(reg, ctx));
  CHECK(reg.size() == 44);

  CHECK(fn<MS uint64_t(*)(const char*)>("GetModuleHandleA")(nullptr) == 0x1234000);
  CHECK(fn<MS uint64_t(*)(const char*)>("GetModuleHandleA")("user32.dll") == 0);

  auto va = fn<MS void*(*)(void*, uint64_t, uint32_t, uint32_t)>("VirtualAlloc");
  auto vf = fn<MS uint32_t(*)(void*, uint64_t, uint32_t)>("VirtualFree");
  void* p = va(nullptr, 100, 0x3000, 4);
  CHECK(p && ((uintptr_t)p % 4096) == 0);
  CHECK(va((void*)0x10000, 100, 0x3000, 4) == nullptr);           // fixed addresses unsupported
  CHECK(vf(p, 100, 0x8000) == 0);                                  // MEM_RELEASE needs size 0
  CHECK(vf(p, 0, 0x4000) == 0);                                    // MEM_DECOMMIT unsupported
  CHECK(vf(p, 0, 0x8000) == 1 && vf(p, 0, 0x8000) == 0);

  auto gsh = fn<MS uint64_t(*)(uint32_t)>("GetStdHandle");
  auto wf = fn<MS uint32_t(*)(uint64_t, const void*, uint32_t, uint32_t*, void*)>("WriteFile");
  uint32_t w = 0;
  CHECK(wf(gsh((uint32_t)-11), "hi", 2, &w, nullptr) == 1 && w == 2 && out == "hi");
  CHECK(gsh(5) == ~0ull);
  CHECK(wf(gsh((uint32_t)-10), "x", 1, &w, nullptr) == 0);         // can't write to stdin

  // files
  auto cf = fn<MS uint64_t(*)(const char*, uint32_t, uint32_t, void*, uint32_t, uint32_t, void*)>("CreateFileA");
  auto rf = fn<MS uint32_t(*)(uint64_t, void*, uint32_t, uint32_t*, void*)>("ReadFile");
  auto sfp = fn<MS uint32_t(*)(uint64_t, int32_t, int32_t*, uint32_t)>("SetFilePointer");
  auto gfs = fn<MS uint32_t(*)(uint64_t, uint32_t*)>("GetFileSize");
  auto ch = fn<MS uint32_t(*)(uint64_t)>("CloseHandle");
  CHECK(cf("../evil", 0x40000000u, 0, nullptr, 2, 0, nullptr) == ~0ull);
  CHECK(cf("nofile.bin", 0x80000000u, 1, nullptr, 3, 0, nullptr) == ~0ull);
  uint64_t f = cf("saves\\g.sav", 0x40000000u, 0, nullptr, 2, 0, nullptr);
  CHECK(f != ~0ull);
  CHECK(wf(f, "abcdef", 6, &w, nullptr) == 1 && w == 6);
  uint32_t hi = 99;
  CHECK(gfs(f, &hi) == 6 && hi == 0);
  CHECK(sfp(f, 2, nullptr, 0) == 2);
  char buf[8] = {0}; uint32_t got = 0;
  CHECK(rf(f, buf, 4, &got, nullptr) == 1 && got == 4 && std::memcmp(buf, "cdef", 4) == 0);
  CHECK(sfp(f, 0, nullptr, 7) == 0xFFFFFFFFu);
  CHECK(ch(f) == 1 && ch(f) == 0 && rf(f, buf, 1, &got, nullptr) == 0);

  // events, sleep, clocks
  auto ce = fn<MS uint64_t(*)(void*, int32_t, int32_t, const char*)>("CreateEventA");
  auto se = fn<MS uint32_t(*)(uint64_t)>("SetEvent");
  auto re = fn<MS uint32_t(*)(uint64_t)>("ResetEvent");
  auto wso = fn<MS uint32_t(*)(uint64_t, uint32_t)>("WaitForSingleObject");
  uint64_t e = ce(nullptr, 1, 0, nullptr);
  CHECK(wso(e, 5) == 0x102 && se(e) == 1 && wso(e, 0) == 0 && re(e) == 1 && wso(e, 0) == 0x102);
  CHECK(wso(0x77777, 0) == 0xFFFFFFFFu && se(0x77777) == 0 && wso(0x1FFFFFFFFull, 0) == 0xFFFFFFFFu);
  auto tick = fn<MS uint64_t(*)()>("GetTickCount64");
  auto sl = fn<MS void(*)(uint32_t)>("Sleep");
  auto qpc = fn<MS uint32_t(*)(int64_t*)>("QueryPerformanceCounter");
  auto qpf = fn<MS uint32_t(*)(int64_t*)>("QueryPerformanceFrequency");
  uint64_t t0 = tick(); int64_t q0 = 0, q1 = 0, fq = 0;
  CHECK(qpc(&q0) == 1 && qpf(&fq) == 1 && fq == 1000000000 && qpc(nullptr) == 0);
  sl(30);
  CHECK(tick() - t0 >= 25 && qpc(&q1) == 1 && (q1 - q0) >= 25000000);

  // critical sections (guest memory address is the key)
  auto ics = fn<MS void(*)(void*)>("InitializeCriticalSection");
  auto ecs = fn<MS void(*)(void*)>("EnterCriticalSection");
  auto tcs = fn<MS uint32_t(*)(void*)>("TryEnterCriticalSection");
  auto lcs = fn<MS void(*)(void*)>("LeaveCriticalSection");
  auto dcs = fn<MS void(*)(void*)>("DeleteCriticalSection");
  alignas(8) char csmem[40];
  CHECK(tcs(csmem) == 0);                                          // not initialized
  ics(csmem); ecs(csmem); ecs(csmem); CHECK(tcs(csmem) == 1); lcs(csmem); lcs(csmem); lcs(csmem);
  dcs(csmem); CHECK(tcs(csmem) == 0);
  ecs(csmem); lcs(csmem); dcs(csmem);                              // harmless on unknown/deleted

  // thread via host API
  auto ct = fn<MS uint64_t(*)(void*, uint64_t, MS uint32_t(*)(void*), void*, uint32_t, uint32_t*)>("CreateThread");
  auto gec = fn<MS uint32_t(*)(uint64_t, uint32_t*)>("GetExitCodeThread");
  struct L { static MS uint32_t run(void* p) { *(int*)p = 11; return 3; } };
  int shared = 0; uint32_t tid = 0, code = 0;
  uint64_t th = ct(nullptr, 0, &L::run, &shared, 0, &tid);
  CHECK(th && tid && wso(th, 0xFFFFFFFFu) == 0 && gec(th, &code) == 1 && code == 3 && shared == 11);
  CHECK(ct(nullptr, 0, nullptr, nullptr, 0, nullptr) == 0);
  std::filesystem::remove_all(root);

  // End to end: hand-built PE allocates memory, starts a thread that reads/writes it, waits, exits.
  {
    std::vector<uint8_t> c;
    emit(c, {0x48,0x83,0xEC,0x48});                         // sub rsp,0x48
    emit(c, {0x31,0xC9});                                   // xor ecx,ecx
    emit(c, {0xBA}); emit32(c, 4096);                       // mov edx,4096
    emit(c, {0x41,0xB8}); emit32(c, 0x3000);                // mov r8d,0x3000
    emit(c, {0x41,0xB9}); emit32(c, 4);                     // mov r9d,4
    emit(c, {0xFF,0x15}); rip(c, 0x1260);                   // call [VirtualAlloc]
    emit(c, {0xC7,0x00}); emit32(c, 0x1234);                // mov dword [rax],0x1234
    emit(c, {0x48,0x89,0x44,0x24,0x40});                    // mov [rsp+0x40],rax
    emit(c, {0x31,0xC9, 0x31,0xD2});                        // xor ecx,ecx ; xor edx,edx
    emit(c, {0x4C,0x8D,0x05}); rip(c, 0x13c0);              // lea r8,[thread routine]
    emit(c, {0x49,0x89,0xC1});                              // mov r9,rax
    emit(c, {0x48,0xC7,0x44,0x24,0x20,0,0,0,0});            // flags = 0
    emit(c, {0x48,0xC7,0x44,0x24,0x28,0,0,0,0});            // thread id ptr = 0
    emit(c, {0xFF,0x15}); rip(c, 0x1268);                   // call [CreateThread]
    emit(c, {0x48,0x89,0x44,0x24,0x38});                    // mov [rsp+0x38],rax
    emit(c, {0x48,0x89,0xC1});                              // mov rcx,rax
    emit(c, {0xBA,0xFF,0xFF,0xFF,0xFF});                    // mov edx,-1
    emit(c, {0xFF,0x15}); rip(c, 0x1270);                   // call [WaitForSingleObject]
    emit(c, {0x48,0x8B,0x4C,0x24,0x38});                    // mov rcx,[rsp+0x38]
    emit(c, {0x48,0x8D,0x54,0x24,0x30});                    // lea rdx,[rsp+0x30]
    emit(c, {0xFF,0x15}); rip(c, 0x1278);                   // call [GetExitCodeThread]
    emit(c, {0x48,0x8B,0x44,0x24,0x40});                    // mov rax,[rsp+0x40]
    emit(c, {0x8B,0x4C,0x24,0x30});                         // mov ecx,[rsp+0x30]   (thread exit code)
    emit(c, {0x03,0x08});                                   // add ecx,[rax]        (0x1234)
    emit(c, {0x03,0x48,0x04});                              // add ecx,[rax+4]      (written by thread)
    emit(c, {0xFF,0x15}); rip(c, 0x1280);                   // call [ExitProcess]
    emit(c, {0xCC});
    CHECK(c.size() < 0x200);
    c.resize(0x3c0, 0);
    emit(c, {0x8B,0x01});                                   // thread: mov eax,[rcx]
    emit(c, {0xC7,0x41,0x04}); emit32(c, 99);               //         mov dword [rcx+4],99
    emit(c, {0xB8}); emit32(c, 5); emit(c, {0xC3});         //         mov eax,5 ; ret
    auto img = pebuild::make(c, {"VirtualAlloc","CreateThread","WaitForSingleObject","GetExitCodeThread","ExitProcess"});
    auto r = run_pe(img.data(), img.size(), true);
    CHECK(r.loaded && r.ran);
    CHECK(r.exit_code == 5 + 0x1234 + 99);
  }
  std::puts("ok"); return 0;
}
