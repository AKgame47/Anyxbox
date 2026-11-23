// Tiny x64 emitter for hand-assembled guest test programs (rip-relative data access, COM vtable calls). Test helper.
#pragma once
#include <cstdint>
#include <initializer_list>
#include <vector>
namespace x64 {
const uint32_t kData = 0x2000;   // .data RVA
const uint32_t kCode = 0x1400;   // code RVA
struct Asm {
  std::vector<uint8_t> c;
  uint32_t rva() const { return kCode + (uint32_t)c.size(); }
  void b(std::initializer_list<int> v) { for (int x : v) c.push_back((uint8_t)x); }
  void d32(uint32_t v) { for (int i = 0; i < 4; i++) c.push_back((uint8_t)(v >> (8 * i))); }
  // rip-relative op: prefix bytes, then disp32 to target rva; `tail` = bytes after the disp
  void rip(std::initializer_list<int> op, uint32_t target, int tail = 0) {
    b(op); d32(target - (rva() + 4 + tail));
  }
  // lea reg,[rip+data]: reg 0=rax 1=rcx 2=rdx 8=r8 9=r9
  void lea(int reg, uint32_t off) { rip({reg >= 8 ? 0x4C : 0x48, 0x8D, 0x05 | ((reg & 7) << 3)}, kData + off); }
  void load(int reg, uint32_t off) { rip({reg >= 8 ? 0x4C : 0x48, 0x8B, 0x05 | ((reg & 7) << 3)}, kData + off); }
  void store_rax(uint32_t off) { rip({0x48, 0x89, 0x05}, kData + off); }
  void store_stack_rax(int disp) { b({0x48, 0x89, 0x44, 0x24, disp}); }
  void zero_stack(int disp) { b({0x48, 0xC7, 0x44, 0x24, disp, 0, 0, 0, 0}); }
  void mov_edx(uint32_t v) { b({0xBA}); d32(v); }
  void mov_r8d(uint32_t v) { b({0x41, 0xB8}); d32(v); }
  void zero_r8() { b({0x4D, 0x31, 0xC0}); }
  void zero_r9() { b({0x45, 0x31, 0xC9}); }
  // after a call: if hr < 0 jump to the fail stub (patched later)
  std::vector<size_t> fails;
  void check() { b({0x85, 0xC0, 0x0F, 0x88}); fails.push_back(c.size()); d32(0); }
  void vcall(int idx) { b({0x48, 0x8B, 0x01, 0xFF, 0x90}); d32(idx * 8); }   // mov rax,[rcx]; call [rax+idx*8]
  void on(uint32_t slot) { load(1, slot); }                                   // rcx = object
  void ptr_to_data(uint32_t off, uint32_t slotoff) { lea(0, off); store_rax(slotoff); }
};


}
