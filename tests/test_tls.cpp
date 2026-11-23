// Anyxbox: PE TLS directory. A hand-built image with a TLS template, an index slot and a callback; the entry reads its
// per-thread block through gs:[0x58][index] exactly like MinGW and MSVC code does.
#include <cstdio>
#include <cstring>
#include <string>
#include "pe_builder.h"
#include "loader/pe_image.h"
#include "runtime/run_pe.h"
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
using namespace pebuild;
static std::vector<uint8_t> image(bool with_callback, uint64_t bad_start = 0) {
  std::vector<uint8_t> code(0x100, 0);
  const uint8_t entry[] = {0x65, 0x48, 0x8b, 0x04, 0x25, 0x58, 0, 0, 0,   // mov rax, gs:[0x58]
                           0x8b, 0x0d, 0, 0, 0, 0,                          // mov ecx, [rip+index]
                           0x48, 0x8b, 0x04, 0xc8,                          // mov rax, [rax+rcx*8]
                           0x8b, 0x50, 0x04,                                // mov edx, [rax+4]
                           0x03, 0x50, 0x0c,                                // add edx, [rax+12]  (zero fill)
                           0x8b, 0x05, 0, 0, 0, 0,                          // mov eax, [rip+flag]
                           0x6b, 0xc0, 0x64, 0x01, 0xd0, 0xc3};             // eax = eax*100 + edx
  std::memcpy(code.data(), entry, sizeof entry);
  w32(code, 11, 0x2200 - (0x1000 + 15));
  w32(code, 27, 0x2400 - (0x1000 + 31));
  auto b = make2(code, {});
  b.resize(0x600 + 0x600, 0);
  size_t c = 0x84, o = c + 20, s2 = o + 240 + 40;
  w32(b, s2 + 16, 0x600); w32(b, s2 + 20, 0x600);
  auto D = [&](uint32_t rva) { return 0x600 + (rva - 0x2000); };
  const uint64_t base = 0x140000000ull;
  w32(b, o + 112 + 9 * 8, 0x2000); w32(b, o + 112 + 9 * 8 + 4, 40);
  w32(b, o + 112 + 5 * 8, 0x2500); w32(b, o + 112 + 5 * 8 + 4, 20);
  w64(b, D(0x2000), bad_start ? bad_start : base + 0x2100); w64(b, D(0x2008), base + 0x2108); w64(b, D(0x2010), base + 0x2200);
  w64(b, D(0x2018), with_callback ? base + 0x2300 : 0); w32(b, D(0x2020), 8);
  w32(b, D(0x2100), 5); w32(b, D(0x2104), 7);
  w32(b, D(0x2200), 99);                       // the loader must overwrite this with index 0
  w64(b, D(0x2300), base + 0x1100); w64(b, D(0x2308), 0);
  w32(b, D(0x2500), 0x2000); w32(b, D(0x2504), 20);
  const uint16_t rel[] = {0xA000 | 0x000, 0xA000 | 0x008, 0xA000 | 0x010, (uint16_t)(with_callback ? 0xA000 | 0x018 : 0), 0xA000 | 0x300, 0};
  for (int i = 0; i < 5; i++) w16(b, D(0x2508 + 2 * i), rel[i]);
  // callback at RVA 0x1100: mov [rip+flag], edx (reason); ret
  size_t cb = 0x200 + 0x100;
  b[cb] = 0x89; b[cb + 1] = 0x15; w32(b, cb + 2, 0x2400 - (0x1100 + 6)); b[cb + 6] = 0xc3;
  return b;
}
int main() {
  {
    auto b = image(true);
    auto img = anyxbox::load_pe_image(b.data(), b.size(), nullptr);
    CHECK(img.ok && img.tls.present && img.tls.callbacks.size() == 1 && img.tls.zero_fill == 8);
    CHECK(img.tls.end_va - img.tls.start_va == 8 && img.tls.start_va == img.actual_base + 0x2100);
    uint32_t idx; std::memcpy(&idx, img.base + 0x2200, 4); CHECK(idx == 0);
  }
  auto b = image(true);
  auto r = anyxbox::run_pe(b.data(), b.size(), true, nullptr);
  std::printf("  ran=%d exit=%u err='%s'\n", r.ran, r.exit_code, r.error.c_str());
  CHECK(r.ran && r.exit_code == 107);          // callback saw DLL_PROCESS_ATTACH (1): 1*100 + tls[1]=7 + zero fill 0
  b = image(false);
  r = anyxbox::run_pe(b.data(), b.size(), true, nullptr);
  CHECK(r.ran && r.exit_code == 7);            // no callback list: flag stays 0
  b = image(true, 0x100);                      // template address outside the image
  auto img = anyxbox::load_pe_image(b.data(), b.size(), nullptr);
  CHECK(!img.ok && img.error.find("tls") != std::string::npos);
  // The mapper accepts in-image callback pointers; launch policy requires code.
  auto reject_callback=[&](std::vector<uint8_t> bytes){
    auto mapped=anyxbox::load_pe_image(bytes.data(),bytes.size(),nullptr);
    if(!mapped.ok)return false;
    auto result=anyxbox::run_pe(bytes.data(),bytes.size(),true);
    return !result.loaded&&!result.ran&&result.output.empty()&&result.error=="TLS callback is not within an executable section";
  };
  const uint64_t base=0x140000000ull;const size_t opt=0x98,section=opt+240;
  for(uint32_t callback:{0x40u,0x2000u,0x2200u,0x1107u}){
    auto bytes=image(true);w64(bytes,0x900,base+callback);
    if(callback==0x1107u)w32(bytes,section+8,0x107); // exact section end, still in executable page
    CHECK(reject_callback(bytes));
  }
  {auto bytes=image(true);w32(bytes,opt+56,0x4000);w32(bytes,section+8,0x107);w64(bytes,0x900,base+0x1fff);
    CHECK(reject_callback(bytes));} // rounded-page tail
  // A valid first callback would fault (UD2) if called. Invalid second must win first.
  {auto bytes=image(true);bytes[0x300]=0x0f;bytes[0x301]=0x0b;
    w64(bytes,0x908,base+0x2200);w64(bytes,0x910,0);
    // Add relocation for the second callback pointer, using the existing padding slot.
    w16(bytes,0xb12,0xA308);
    CHECK(reject_callback(bytes));}
  // First-byte executable extent is enough for this policy; instruction validity is separate.
  {auto bytes=image(true);w32(bytes,section+8,0x107);w32(bytes,section+16,0x107);
    auto result=anyxbox::run_pe(bytes.data(),bytes.size(),true);
    CHECK(result.loaded&&result.ran&&result.exit_code==107);}
  std::puts("TLS launch policy: header/data/exact-end/page-tail callbacks and invalid later list member rejected before execution");
  std::puts("test_tls ok");
  return 0;
}
