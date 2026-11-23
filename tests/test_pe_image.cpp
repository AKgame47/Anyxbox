#include "loader/pe_image.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace anyxbox;
static void w16(std::vector<uint8_t>& b, size_t o, uint16_t v){std::memcpy(&b[o],&v,2);}
static void w32(std::vector<uint8_t>& b, size_t o, uint32_t v){std::memcpy(&b[o],&v,4);}
static void w64(std::vector<uint8_t>& b, size_t o, uint64_t v){std::memcpy(&b[o],&v,8);}
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)

// Builds a one-section PE: .data at RVA 0x1000 holds a pointer needing a reloc,
// an import table (kernel32!ExitProcess by name, d3d11.dll ordinal 7).
static std::vector<uint8_t> build() {
  std::vector<uint8_t> b(0x600, 0);
  b[0]='M'; b[1]='Z'; w32(b,0x3c,0x80);
  std::memcpy(&b[0x80],"PE\0\0",4);
  size_t c=0x84; w16(b,c,0x8664); w16(b,c+2,1); w16(b,c+16,240);
  size_t o=c+20; w16(b,o,0x20b); w32(b,o+16,0x1000); w64(b,o+24,0x13c000000ull);
  w32(b,o+32,0x1000); w32(b,o+56,0x2000); w32(b,o+60,0x200); w32(b,o+108,16);
  w32(b,o+112+1*8,0x1100); w32(b,o+112+1*8+4,60);   // imports
  w32(b,o+112+5*8,0x13c0); w32(b,o+112+5*8+4,12);   // relocs
  size_t s=o+240; std::memcpy(&b[s],".data",5);
  w32(b,s+8,0x1000); w32(b,s+12,0x1000); w32(b,s+16,0x400); w32(b,s+20,0x200);
  size_t raw=0x200;                                  // RVA = 0x1000 + (file - 0x200)
  auto R=[&](uint32_t rva){return raw+(rva-0x1000);};
  w64(b,R(0x1000),0x13c001234ull);                   // pointer to relocate
  // descriptor 0: kernel32
  w32(b,R(0x1100),0x1200); w32(b,R(0x110c),0x1180); w32(b,R(0x1110),0x1220);
  // descriptor 1: d3d11
  w32(b,R(0x1114),0x1240); w32(b,R(0x1120),0x1190); w32(b,R(0x1124),0x1260);
  std::memcpy(&b[R(0x1180)],"kernel32.dll",13);
  std::memcpy(&b[R(0x1190)],"d3d11.dll",10);
  w64(b,R(0x1200),0x12a0); w64(b,R(0x1220),0x12a0);  // by name
  w64(b,R(0x1240),(1ull<<63)|7); w64(b,R(0x1260),(1ull<<63)|7);
  std::memcpy(&b[R(0x12a0)+2],"ExitProcess",12);
  w32(b,R(0x13c0),0x1000); w32(b,R(0x13c4),12); w16(b,R(0x13c8),(10<<12)|0);
  return b;
}
int main() {
  auto b = build();
  auto resolver = [](const std::string& dll, const std::string& n, uint16_t ord, bool byo)->uint64_t{
    if (dll=="kernel32.dll" && n=="ExitProcess") return 0xAAAA;
    if (dll=="d3d11.dll" && byo && ord==7) return 0xBBBB;
    return 0; };
  auto img = load_pe_image(b.data(), b.size(), resolver);
  CHECK(img.ok);
  CHECK(img.entry_rva==0x1000 && img.mem.size()==0x2000);
  CHECK(img.reloc_count==1);
  uint64_t p; std::memcpy(&p,&img.mem[0x1000],8);
  CHECK(p == 0x13c001234ull + (img.actual_base - 0x13c000000ull));
  CHECK(img.imports.size()==2);
  uint64_t a; std::memcpy(&a,&img.mem[0x1220],8); CHECK(a==0xAAAA);
  std::memcpy(&a,&img.mem[0x1260],8); CHECK(a==0xBBBB);
  // unresolved import is reported
  auto bad = load_pe_image(b.data(), b.size(), [](auto&&...)->uint64_t{return 0;});
  CHECK(!bad.ok && bad.unresolved.size()==2);
  // truncated file and bad machine rejected
  CHECK(!load_pe_image(b.data(), 0x100, resolver).ok);
  auto c = b; c[0x84]=0x4c; CHECK(!load_pe_image(c.data(), c.size(), resolver).ok);
  // section pointing outside image rejected
  auto d = b; w32(d,0x84+20+240+12,0x1f00); CHECK(!load_pe_image(d.data(), d.size(), resolver).ok);
  std::puts("ok"); return 0;
}
