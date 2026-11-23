// import_gap <file.exe>...: for each PE, list every import and whether Anyxbox has a host implementation.
// Prints missing imports grouped by DLL, and a count of what is covered. Nothing is executed.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <vector>
#include "loader/pe_image.h"
#include "runtime/d3d11_runtime.h"
#include "runtime/kernel32.h"
#include "runtime/msvcrt.h"
#include "runtime/user32.h"
int main(int argc, char** argv) {
  if (argc < 2) { std::puts("usage: import_gap <file.exe>..."); return 2; }
  anyxbox::ImportRegistry reg;
  anyxbox::Kernel32Context k; anyxbox::MsvcrtContext crt; anyxbox::User32Context u; anyxbox::D3D11Runtime rt;
  anyxbox::kernel32_register(reg, k); anyxbox::msvcrt_register(reg, crt); anyxbox::user32_register(reg, u); rt.register_imports(reg);
  int rc = 0;
  for (int i = 1; i < argc; i++) {
    std::ifstream f(argv[i], std::ios::binary);
    if (!f) { std::printf("%s: cannot open\n", argv[i]); rc = 2; continue; }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    auto img = anyxbox::load_pe_image(d.data(), d.size(), [&](const std::string& dll, const std::string& n, uint16_t o, bool b) {
      return reg.resolve(dll, n, o, b); });
    if (img.imports.empty() && !img.ok) { std::printf("%s: not loadable: %s\n", argv[i], img.error.c_str()); rc = 1; continue; }
    std::map<std::string, std::set<std::string>> missing;
    for (auto& s : img.unresolved) { size_t p = s.find('!'); missing[s.substr(0, p)].insert(s.substr(p + 1)); }
    std::printf("%s: %zu imports, %zu covered, %zu missing\n", argv[i], img.imports.size(), img.imports.size() - img.unresolved.size(), img.unresolved.size());
    for (auto& kv : missing) { std::printf("  %s (%zu):", kv.first.c_str(), kv.second.size()); for (auto& n : kv.second) std::printf(" %s", n.c_str()); std::puts(""); }
    if (!img.unresolved.empty()) rc = 1;
  }
  return rc;
}
