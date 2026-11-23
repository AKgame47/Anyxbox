// Anyxbox: runs the logic of tests/fixtures/triangle.c (the real fixture source, compiled here as a host function with its
// imports bound to Anyxbox host functions) through user32, the D3D11 COM runtime and the window present path.
// This is NOT the MinGW-built exe: it proves the fixture's call sequence works against our runtime. Linux x86-64 only.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#define ANYXBOX_HOST_TEST 1
#include "../tests/fixtures/triangle.c"
#include "gfx/d3d11_vulkan.h"
#include "runtime/kernel32.h"
#include "runtime/window_present.h"
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
using namespace anyxbox;
struct ImportEntry { const char* dll; const char* name; void** slot; };
static std::vector<ImportEntry>& imports() { static std::vector<ImportEntry> v; return v; }
extern "C" void anyxbox_fixture_import(const char* dll, const char* name, void** slot) { imports().push_back({dll, name, slot}); }
struct Fake : HostWindow {
  std::vector<HostEvent> ev; size_t at = 0; int opens = 0, presents = 0, closes = 0; uint32_t ow = 0, oh = 0;
  bool open(uint32_t w, uint32_t h, const char*) override { opens++; ow = w; oh = h; return true; }
  void present(const uint8_t*, uint32_t, uint32_t) override { presents++; }
  bool poll(HostEvent& e) override { if (at >= ev.size()) return false; e = ev[at++]; return true; }
  void close() override { closes++; }
};
static __attribute__((ms_abi)) void stub_exit(uint32_t) {}
static int bind(ImportRegistry& reg) {
  reg.add("kernel32.dll", "ExitProcess", (uint64_t)(uintptr_t)&stub_exit);   // run_pe provides the real one
  int n = 0;
  for (ImportEntry* e = imports().data(); e != imports().data() + imports().size(); e++) {
    uint64_t a = reg.resolve(e->dll, e->name, 0, false);
    if (!a) { std::printf("unresolved import %s!%s\n", e->dll, e->name); return -1; }
    *e->slot = (void*)(uintptr_t)a; n++;
  }
  return n;
}
static const uint8_t* px(const std::vector<uint8_t>& im, uint32_t w, int x, int y) { return &im[((size_t)y * w + x) * 4]; }
int main() {
  Fake host; User32Context u; u.host = &host; D3D11Runtime rt; ImportRegistry reg; Kernel32Context k;
  kernel32_register(reg, k); user32_register(reg, u); rt.register_imports(reg); attach_window_present(rt, u);
  int n = bind(reg); CHECK(n == 10);
  uint32_t rc = entry();
  std::printf("fixture exit %u, presents %d, window presents %d, last_error '%s'\n", rc, rt.presents, host.presents, rt.last_error.c_str());
  CHECK(rc == 0 && rt.last_error.empty());
  CHECK(rt.presents == 3 && host.presents == 3 && host.opens == 1 && host.ow == 320 && host.oh == 240);
  CHECK(rt.presented_w == 320 && rt.presented_h == 240 && rt.shaders_translated == 2);
  auto& im = rt.presented;
  CHECK(px(im, 320, 4, 4)[2] > 25 && px(im, 320, 4, 4)[0] < 25);                                 // clear colour (0.05, 0.05, 0.15)
  const uint8_t* c = px(im, 320, 160, 130);
  CHECK(c[0] > 40 && c[1] > 40 && c[2] > 40 && c[0] < 140 && c[1] < 140 && c[2] < 140);           // blended inside the triangle
  const uint8_t* top = px(im, 320, 160, 58);   CHECK(top[0] > 200 && top[1] < 60 && top[2] < 60);   // red apex
  const uint8_t* br = px(im, 320, 248, 185);   CHECK(br[1] > 150 && br[0] < 90);                    // near the green corner
  const uint8_t* bl = px(im, 320, 72, 185);    CHECK(bl[2] > 150 && bl[0] < 90);                    // near the blue corner
  std::printf("triangle fixture: apex (%u,%u,%u) centre (%u,%u,%u) clear (%u,%u,%u)\n", top[0], top[1], top[2], c[0], c[1], c[2], px(im, 320, 4, 4)[0], px(im, 320, 4, 4)[1], px(im, 320, 4, 4)[2]);
  // closing the window before drawing: the loop ends with exit 0 and no frame
  { Fake h2; h2.ev = {{HostEvent::Quit, 0, 0, 0, 0}}; User32Context u2; u2.host = &h2; D3D11Runtime rt2; ImportRegistry r2; Kernel32Context k2;
    kernel32_register(r2, k2); user32_register(r2, u2); rt2.register_imports(r2); attach_window_present(rt2, u2); CHECK(bind(r2) == 10);
    CHECK(entry() == 0 && rt2.presents == 0 && h2.closes >= 1); }
  // Vulkan backend (software driver here): same output as the reference renderer
  pnr::graphics::VulkanBackend vk;
  auto s = vk.open();
  if (!s) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); std::puts("test_fixture_source ok"); return 0; }
  s = vk.create_instance("anyxbox-test");
  if (!s || vk.devices().empty()) { std::printf("SKIP gpu part: no device\n"); std::puts("test_fixture_source ok"); return 0; }
  if (!(s = vk.create_device(0))) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); std::puts("test_fixture_source ok"); return 0; }
  { D3D11Runtime gpu; d3d11::VulkanDrawBackend be(vk); gpu.ctx->SetDrawBackend(&be);
    gpu.on_shaders = [&](const std::vector<uint32_t>& v, const std::vector<uint32_t>& p) { be.SetShaders(v, p); };
    Fake h3; User32Context u3; u3.host = &h3; ImportRegistry r3; Kernel32Context k3;
    kernel32_register(r3, k3); user32_register(r3, u3); gpu.register_imports(r3); attach_window_present(gpu, u3); CHECK(bind(r3) == 10);
    CHECK(entry() == 0 && gpu.last_error.empty() && gpu.presents == 3 && h3.presents == 3);
    int bad = 0;
    for (size_t i = 0; i + 3 < im.size(); i += 4) { int d = 0; for (int q = 0; q < 3; q++) d = std::max(d, std::abs((int)im[i + q] - (int)gpu.presented[i + q])); if (d > 6) bad++; }
    std::printf("fixture triangle, Vulkan vs reference: mismatching pixels %d of %zu\n", bad, im.size() / 4);
    CHECK(bad < (int)(im.size() / 4 / 100)); }
  std::puts("test_fixture_source ok");
  return 0;
}
