// Anyxbox fixture slot: import coverage for hand-built PEs, and for any real self-built *.exe in tests/fixtures.
// Usage: test_fixture_slot [fixture_dir]. Without fixtures it only runs the built-in import check.
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "pe_builder.h"
#include "loader/pe_image.h"
#include "runtime/d3d11_runtime.h"
#include "runtime/kernel32.h"
#include "runtime/msvcrt.h"
#include "runtime/run_pe.h"
#include "runtime/user32.h"
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
using namespace anyxbox;
static __attribute__((ms_abi)) void stub_exit(uint32_t) {}
static std::vector<std::string> gaps(const std::vector<uint8_t>& pe, size_t* total) {
  ImportRegistry reg; Kernel32Context k; MsvcrtContext crt; User32Context u; D3D11Runtime rt;
  kernel32_register(reg, k); msvcrt_register(reg, crt); user32_register(reg, u); rt.register_imports(reg);
  reg.add("kernel32.dll", "ExitProcess", (uint64_t)(uintptr_t)&stub_exit);   // run_pe binds the real one
  auto img = load_pe_image(pe.data(), pe.size(), [&](const std::string& d, const std::string& n, uint16_t o, bool b) { return reg.resolve(d, n, o, b); });
  if (total) *total = img.imports.size();
  return img.unresolved;
}
int main(int argc, char** argv) {
  std::vector<uint8_t> code(16, 0x90);
  size_t total = 0;
  auto g = gaps(pebuild::make(code, {"CreateWindowExA", "PeekMessageA", "NoSuchUserFunction"}, "user32.dll"), &total);
  CHECK(total == 3 && g.size() == 1 && g[0] == "user32.dll!NoSuchUserFunction");
  g = gaps(pebuild::make(code, {"CreateFileA", "Sleep"}, "kernel32.dll"), &total);
  CHECK(total == 2 && g.empty());
  g = gaps(pebuild::make(code, {"D3D11CreateDevice"}, "d3d11.dll"), &total);
  CHECK(g.empty());
  g = gaps(pebuild::make(code, {"Anything"}, "ntdll.dll"), &total);
  CHECK(g.size() == 1);
  std::string dir = argc > 1 ? argv[1] : "tests/fixtures";
  int found = 0;
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* e = readdir(d)) {
      std::string n = e->d_name;
      if (n.size() < 5 || n.substr(n.size() - 4) != ".exe") continue;
      std::ifstream f(dir + "/" + n, std::ios::binary);
      std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
      found++;
      auto miss = gaps(data, &total);
      std::printf("fixture %s: %zu imports, %zu missing\n", n.c_str(), total, miss.size());
      for (auto& m : miss) std::printf("  missing %s\n", m.c_str());
      if (n.size() > 8 && n.substr(n.size() - 8) == ".run.exe") {
        CHECK(miss.empty());
        User32Context u; D3D11Runtime rt;
        auto r = run_pe(data.data(), data.size(), true, [&](ImportRegistry& reg) { user32_register(reg, u); rt.register_imports(reg); });
        std::printf("  ran=%d exit=%u output=%s\n", r.ran, r.exit_code, r.output.c_str());
        CHECK(r.ran);
      }
    }
    closedir(d);
  }
  std::printf("test_fixture_slot ok (%d real fixtures)\n", found);
  return 0;
}
