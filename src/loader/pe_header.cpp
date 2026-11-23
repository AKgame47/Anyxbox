#include "loader/pe_header.h"
#include <cstring>
namespace anyxbox {
static uint32_t rd32(const uint8_t* p){uint32_t v;std::memcpy(&v,p,4);return v;}
static uint16_t rd16(const uint8_t* p){uint16_t v;std::memcpy(&v,p,2);return v;}
static uint64_t rd64(const uint8_t* p){uint64_t v;std::memcpy(&v,p,8);return v;}
PeInfo parse_pe_header(const uint8_t* d, size_t n) {
  PeInfo r{false,false,0,0,0};
  if (n < 0x40 || d[0]!='M' || d[1]!='Z') return r;
  uint32_t pe = rd32(d+0x3c);
  if ((uint64_t)pe + 24 + 112 > n || std::memcmp(d+pe,"PE\0\0",4)!=0) return r;
  const uint8_t* coff = d+pe+4;
  if (rd16(coff) != 0x8664) return r;           // x86-64 only
  r.sections = rd16(coff+2);
  const uint8_t* opt = coff+20;
  if (rd16(opt) != 0x20b) return r;             // PE32+
  r.is64 = true;
  r.entry_rva = rd32(opt+16);
  r.image_base = rd64(opt+24);
  r.ok = true;
  return r;
}
}
