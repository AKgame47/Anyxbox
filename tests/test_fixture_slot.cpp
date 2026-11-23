// Anyxbox fixture slot: import coverage for hand-built PEs, and for any real self-built *.exe in tests/fixtures.
// Usage: test_fixture_slot [fixture_dir]. Without fixtures it only runs the built-in import check.
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "pe_builder.h"
#include "loader/pe_image.h"
#include "gfx/d3d11_vulkan.h"
#include "runtime/d3d11_runtime.h"
#include "runtime/devices.h"
#include "runtime/kernel32.h"
#include "runtime/msvcrt.h"
#include "runtime/run_pe.h"
#include "runtime/user32.h"
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
using namespace anyxbox;
static __attribute__((ms_abi)) void stub_exit(uint32_t) {}
static std::vector<std::string> gaps(const std::vector<uint8_t>& pe, size_t* total) {
  ImportRegistry reg; Kernel32Context k; MsvcrtContext crt; User32Context u; D3D11Runtime rt;
  kernel32_register(reg, k); msvcrt_register(reg, crt); user32_register(reg, u); rt.register_imports(reg); XInputContext xi; xinput_register(reg, xi); XAudio2Context xa; xaudio2_register(reg, xa);
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
        CHECK(r.ran && r.loaded && r.exit_code == 0);
        if (n.find("hello_crt") == 0) CHECK(r.output == "hello from a MinGW program, argc=1\n");
        if (n.find("tls_crt") == 0) CHECK(r.output == "tls=42 zero=0\n");
        if (n.find("scene") == 0) {
          CHECK(rt.presents == 2 && rt.presented_w == 320 && rt.presented_h == 240 && rt.last_error.empty() && rt.shaders_translated == 4);
          // pixels need the translated shaders: run again on the Vulkan backend (software driver) when one exists
          pnr::graphics::VulkanBackend vk;
          if (vk.open() && vk.create_instance("anyxbox-test") && !vk.devices().empty() && vk.create_device(0)) {
            D3D11Runtime g; d3d11::VulkanDrawBackend be(vk); g.ctx->SetDrawBackend(&be);
            g.on_shaders_ubo = [&](const std::vector<uint32_t>& v, const std::vector<uint32_t>& p, bool ubo) { be.SetShaders(v, p, ubo); };
            User32Context u2;
            auto r2 = run_pe(data.data(), data.size(), true, [&](ImportRegistry& reg) { user32_register(reg, u2); g.register_imports(reg); });
            CHECK(r2.ran && r2.exit_code == 0 && g.presents == 2 && g.last_error.empty());
            auto Q = [&](int x, int y) { return &g.presented[((size_t)y * 320 + x) * 4]; };
            auto is = [&](const uint8_t* q, int r, int gg, int b) { return std::abs(q[0] - r) <= 4 && std::abs(q[1] - gg) <= 4 && std::abs(q[2] - b) <= 4; };
            CHECK(is(Q(300, 10), 13, 13, 38) && is(Q(50, 60), 0, 255, 0) && is(Q(190, 150), 255, 128, 0) && is(Q(120, 100), 0, 255, 0));
            CHECK(is(Q(240, 70), 255, 0, 0) && is(Q(290, 70), 0, 255, 0) && is(Q(240, 170), 0, 0, 255) && is(Q(290, 170), 255, 255, 255));
            std::printf("  scene pixels verified on the Vulkan backend\n");
          } else std::printf("  SKIP scene pixels: no Vulkan device\n");
        }
        if (n.find("triangle") == 0) {
          CHECK(rt.presents == 3 && rt.presented_w == 320 && rt.presented_h == 240 && rt.last_error.empty());
          auto P = [&](int x, int y) { return &rt.presented[((size_t)y * 320 + x) * 4]; };
          CHECK(P(4, 4)[2] > 25 && P(4, 4)[0] < 25);                                         // clear colour
          CHECK(P(160, 58)[0] > 200 && P(160, 58)[1] < 60 && P(160, 58)[2] < 60);            // red apex
          CHECK(P(248, 185)[1] > 150 && P(248, 185)[0] < 90 && P(72, 185)[2] > 150 && P(72, 185)[0] < 90);   // green and blue corners
        }
      }
    }
    closedir(d);
  }
  std::printf("test_fixture_slot ok (%d real fixtures)\n", found);
  return 0;
}
