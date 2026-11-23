// Builds tiny PE32+ files by hand (no compiler needed). Test helper.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace pebuild {
inline void w16(std::vector<uint8_t>& b, size_t o, uint16_t v){std::memcpy(&b[o],&v,2);}
inline void w32(std::vector<uint8_t>& b, size_t o, uint32_t v){std::memcpy(&b[o],&v,4);}
inline void w64(std::vector<uint8_t>& b, size_t o, uint64_t v){std::memcpy(&b[o],&v,8);}
// One RWX section at RVA 0x1000 (raw 0x200..0x600). Code at RVA 0x1000. If imports non-empty,
// import dir for kernel32.dll is placed at 0x1200 and IAT slot i is at RVA 0x1260 + 8*i.
inline std::vector<uint8_t> make(const std::vector<uint8_t>& code, const std::vector<std::string>& imports, const std::string& dll = "kernel32.dll") {
  std::vector<uint8_t> b(0x600, 0);
  b[0]='M'; b[1]='Z'; w32(b,0x3c,0x80);
  std::memcpy(&b[0x80],"PE\0\0",4);
  size_t c=0x84; w16(b,c,0x8664); w16(b,c+2,1); w16(b,c+16,240);
  size_t o=c+20; w16(b,o,0x20b); w32(b,o+16,0x1000); w64(b,o+24,0x140000000ull);
  w32(b,o+32,0x1000); w32(b,o+56,0x2000); w32(b,o+60,0x200); w32(b,o+108,16);
  if (!imports.empty()) { w32(b,o+112+8,0x1200); w32(b,o+112+8+4,40); }
  size_t s=o+240; std::memcpy(&b[s],".text",5);
  w32(b,s+8,0x1000); w32(b,s+12,0x1000); w32(b,s+16,0x400); w32(b,s+20,0x200);
  auto R=[&](uint32_t rva){return 0x200+(rva-0x1000);};
  std::memcpy(&b[R(0x1000)], code.data(), code.size());
  if (!imports.empty()) {
    w32(b,R(0x1200),0x1240); w32(b,R(0x120c),0x1230); w32(b,R(0x1210),0x1260);
    std::memcpy(&b[R(0x1230)], dll.c_str(), dll.size()+1);
    uint32_t nm=0x1290;
    for (size_t i=0;i<imports.size();i++) {
      w64(b,R(0x1240+8*i),nm); w64(b,R(0x1260+8*i),nm);
      std::memcpy(&b[R(nm)+2], imports[i].c_str(), imports[i].size()+1);
      nm += 0x20;
    }
  }
  return b;
}
}
