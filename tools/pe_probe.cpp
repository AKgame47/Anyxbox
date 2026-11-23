// pe_probe <file.exe>: load a PE with an empty import registry and print what is missing.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include "loader/pe_report.h"
#include "syslayer/import_registry.h"
int main(int argc, char** argv) {
  if (argc != 2) { std::puts("usage: pe_probe <file.exe>"); return 2; }
  std::ifstream f(argv[1], std::ios::binary);
  if (!f) { std::puts("cannot open file"); return 2; }
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
  anyxbox::ImportRegistry reg;  // grows as the system layer grows
  auto img = anyxbox::load_pe_image(d.data(), d.size(), [&](const std::string& dll, const std::string& n, uint16_t o, bool b) { return reg.resolve(dll, n, o, b); });
  std::fputs(anyxbox::pe_report(img).c_str(), stdout);
  return img.ok ? 0 : 1;
}
