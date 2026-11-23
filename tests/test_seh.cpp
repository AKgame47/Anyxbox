// Anyxbox: table-based SEH. Hand-assembled x64 code with .pdata/unwind info and C scope tables (what MSVC and MinGW emit
// for __try/__except/__finally): RaiseException walks the frames, the filter decides, RtlUnwindEx runs the __finally and
// continues in the __except block of an outer frame.
#include <cstdio>
#include <cstring>
#include <string>
#include "pe_builder.h"
#include "runtime/run_pe.h"
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
using namespace pebuild;
// filter: 0 = EXCEPTION_EXECUTE_HANDLER (filter function returning 1), 1 = CONTINUE_SEARCH (returns 0), 2 = CONTINUE_EXECUTION (returns -1),
//         3 = handler address is the constant 1 (EXCEPTION_EXECUTE_HANDLER literal)
// fault: 0 = RaiseException(0xE0000001), 1 = read of address 0, 2 = integer divide by zero. With filter 4 the filter function
// returns 1 only when the exception code in EXCEPTION_POINTERS equals want.
static std::vector<uint8_t> image(int filter, bool inner_raises = true, int fault = 0, uint32_t want = 0) {
  auto b = make(std::vector<uint8_t>(1, 0xC3), {"RaiseException", "__C_specific_handler"});
  b.resize(0xA00, 0);
  size_t c = 0x84, o = c + 20, s = o + 240;
  w32(b, s + 16, 0x800);                                   // raw size of .text: RVA 0x1000..0x1800 is backed by the file
  auto R = [&](uint32_t rva) { return (size_t)(0x200 + (rva - 0x1000)); };
  auto put = [&](uint32_t rva, std::initializer_list<uint8_t> v) { size_t i = R(rva); for (uint8_t x : v) b[i++] = x; };
  auto rel = [&](uint32_t at, uint32_t len, uint32_t target) { w32(b, R(at + len - 4), target - (at + len)); };
  // inner @0x1000: sub rsp,0x28 ; try { RaiseException(0xE0000001,0,0,0) ; nop } __finally { marker = 1 } ; add rsp,0x28 ; ret
  put(0x1000, {0x48, 0x83, 0xEC, 0x28});
  if (inner_raises) { put(0x1004, {0xB9, 0x01, 0x00, 0x00, 0xE0}); } else { put(0x1004, {0xEB, 0x11}); }   // skip the raise
  put(0x1009, {0x31, 0xD2, 0x45, 0x31, 0xC0, 0x45, 0x31, 0xC9});
  put(0x1011, {0xFF, 0x15, 0, 0, 0, 0}); rel(0x1011, 6, 0x1260);                          // call [IAT RaiseException]
  if (fault == 1) put(0x1004, {0x8B, 0x04, 0x25, 0, 0, 0, 0});          // mov eax, [0]
  if (fault == 2) put(0x1004, {0x31, 0xC9, 0xF7, 0xF1});                // xor ecx,ecx ; div ecx
  put(0x1017, {0x90});
  put(0x1018, {0x48, 0x83, 0xC4, 0x28, 0xC3});
  // outer @0x1100
  put(0x1100, {0x48, 0x83, 0xEC, 0x28});
  put(0x1104, {0xE8, 0, 0, 0, 0}); rel(0x1104, 5, 0x1000);                                 // call inner
  put(0x1109, {0x90});
  put(0x110A, {0xB8, 0x01, 0x00, 0x00, 0x00});                                              // normal path: eax = 1
  put(0x110F, {0xEB, 0x09});
  put(0x1111, {0x8B, 0x05, 0, 0, 0, 0}); rel(0x1111, 6, 0x1700);                           // __except block: eax = marker
  put(0x1117, {0x83, 0xC0, 0x29});                                                          //                 eax += 41
  put(0x111A, {0x48, 0x83, 0xC4, 0x28, 0xC3});
  // filters, finally, thunk
  put(0x1180, {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
  put(0x1190, {0x31, 0xC0, 0xC3});
  put(0x11A0, {0xC7, 0x05, 0, 0, 0, 0, 0x01, 0x00, 0x00, 0x00, 0xC3}); w32(b, R(0x11A2), 0x1700 - (0x11A0 + 10));   // finally: marker = 1
  put(0x11B0, {0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0xC3});
  put(0x11D0, {0x48, 0x8B, 0x01, 0x81, 0x38, 0, 0, 0, 0, 0x0F, 0x94, 0xC0, 0x0F, 0xB6, 0xC0, 0xC3}); w32(b, R(0x11D5), want);   // return rec->ExceptionCode == want
  put(0x11C0, {0xFF, 0x25, 0, 0, 0, 0}); rel(0x11C0, 6, 0x1268);                          // jmp [IAT __C_specific_handler]
  // unwind info: inner (EHANDLER|UHANDLER), outer (EHANDLER)
  put(0x1400, {0x19, 0x04, 0x01, 0x00, 0x04, 0x42, 0x00, 0x00});
  w32(b, R(0x1408), 0x11C0); w32(b, R(0x140C), 1); w32(b, R(0x1410), 0x1004); w32(b, R(0x1414), 0x1018); w32(b, R(0x1418), 0x11A0); w32(b, R(0x141C), 0);
  put(0x1440, {0x09, 0x04, 0x01, 0x00, 0x04, 0x42, 0x00, 0x00});
  w32(b, R(0x1448), 0x11C0); w32(b, R(0x144C), 1); w32(b, R(0x1450), 0x1104); w32(b, R(0x1454), 0x110A);
  uint32_t f = filter == 0 ? 0x1180 : filter == 1 ? 0x1190 : filter == 2 ? 0x11B0 : filter == 4 ? 0x11D0 : 1;
  w32(b, R(0x1458), f); w32(b, R(0x145C), 0x1111);
  // .pdata
  w32(b, R(0x1500), 0x1000); w32(b, R(0x1504), 0x101D); w32(b, R(0x1508), 0x1400);
  w32(b, R(0x150C), 0x1100); w32(b, R(0x1510), 0x111F); w32(b, R(0x1514), 0x1440);
  w32(b, o + 112 + 3 * 8, 0x1500); w32(b, o + 112 + 3 * 8 + 4, 24);
  w32(b, o + 16, 0x1100);                                                                   // entry = outer
  return b;
}
int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  auto run = [&](int filter, bool raises = true, int fault = 0, uint32_t want = 0) { auto b = image(filter, raises, fault, want); return anyxbox::run_pe(b.data(), b.size(), true, [&](anyxbox::ImportRegistry& reg) {
      reg.add("kernel32.dll", "__C_specific_handler", reg.resolve("msvcrt.dll", "__C_specific_handler", 0, false)); }); };
  auto r = run(0, false);
  std::printf("  no exception: ran=%d exit=%u err='%s' unresolved=%zu\n", r.ran, r.exit_code, r.error.c_str(), r.unresolved.size());
  CHECK(r.ran && r.exit_code == 1);                                  // the normal path never touches SEH
  r = run(0);
  std::printf("  caught: ran=%d exit=%u\n", r.ran, r.exit_code);
  CHECK(r.ran && r.exit_code == 42);                                 // filter said execute: __finally ran (marker 1 + 41), __except block returned it
  r = run(3);
  CHECK(r.ran && r.exit_code == 42);                                 // literal EXCEPTION_EXECUTE_HANDLER in the scope table
  r = run(2);
  CHECK(r.ran && r.exit_code == 1);                                  // EXCEPTION_CONTINUE_EXECUTION: RaiseException returns, no unwind, __finally not run
  r = run(1);
  std::printf("  not handled: ran=%d exit=0x%X\n", r.ran, r.exit_code);
  CHECK(r.ran && r.exit_code == 0xE0000001u);                        // CONTINUE_SEARCH everywhere: the program ends with the exception code
  r = run(4, true, 1, 0xC0000005u);
  std::printf("  access violation: ran=%d exit=%u\n", r.ran, r.exit_code);
  CHECK(r.ran && r.exit_code == 42);                                 // SIGSEGV in guest code became an exception, filter saw 0xC0000005, __finally ran
  r = run(4, true, 2, 0xC0000094u);
  CHECK(r.ran && r.exit_code == 42);                                 // SIGFPE (integer divide by zero) -> 0xC0000094
  r = run(4, true, 1, 0xC0000094u);
  std::printf("  wrong code: ran=%d exit=0x%X\n", r.ran, r.exit_code);
  CHECK(r.ran && r.exit_code == 0xC0000005u);                        // filter refuses: unhandled access violation ends the program with its code
  r = run(0, true, 2);
  CHECK(r.ran && r.exit_code == 42);                                 // a runtime that works after earlier faults (handlers stay installed)
  std::puts("test_seh ok");
  return 0;
}
