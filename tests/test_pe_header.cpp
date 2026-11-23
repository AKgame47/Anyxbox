#include "loader/pe_header.h"
#include <cstdio>
#include <cstring>
#include <vector>
int main() {
  std::vector<uint8_t> b(512, 0);
  b[0]='M'; b[1]='Z'; b[0x3c]=0x80;
  std::memcpy(&b[0x80], "PE\0\0", 4);
  b[0x84]=0x64; b[0x85]=0x86; b[0x86]=3;        // machine, 3 sections
  b[0x98]=0x0b; b[0x99]=0x02;                   // PE32+
  b[0xa8]=0x00; b[0xa9]=0x10;                   // entry 0x1000
  auto r = anyxbox::parse_pe_header(b.data(), b.size());
  if (!r.ok || !r.is64 || r.sections!=3 || r.entry_rva!=0x1000) { std::puts("FAIL"); return 1; }
  b[0]='X';
  if (anyxbox::parse_pe_header(b.data(), b.size()).ok) { std::puts("FAIL bad magic"); return 1; }
  std::puts("ok"); return 0;
}
