// Anyxbox: runs the logic of tests/fixtures/scene.c (the real fixture source, compiled here as a host function with its
// imports bound to Anyxbox host functions) through user32, the D3D11 COM runtime and the window present path.
// This is NOT the MinGW-built exe: it proves the fixture's call sequence works against our runtime. Linux x86-64 only.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#define ANYXBOX_HOST_TEST 1
#include "../tests/fixtures/scene.c"
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
  CHECK(rt.presents == 2 && host.presents == 2 && host.opens == 1 && rt.presented_w == 320 && rt.presented_h == 240);
  CHECK(rt.shaders_translated == 4);
  // The reference renderer ignores shader bytecode, so pixels are checked on the Vulkan run below.
  // closing the window before drawing: the loop ends with exit 0 and no frame
  { Fake h2; h2.ev = {{HostEvent::Quit, 0, 0, 0, 0}}; User32Context u2; u2.host = &h2; D3D11Runtime rt2; ImportRegistry r2; Kernel32Context k2;
    kernel32_register(r2, k2); user32_register(r2, u2); rt2.register_imports(r2); attach_window_present(rt2, u2); CHECK(bind(r2) == 10);
    CHECK(entry() == 0 && rt2.presents == 0 && h2.closes >= 1); }
  // Vulkan backend (software driver here): the translated shaders decide the pixels
  pnr::graphics::VulkanBackend vk;
  auto s = vk.open();
  if (!s) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); std::puts("test_fixture_scene ok"); return 0; }
  s = vk.create_instance("anyxbox-test");
  if (!s || vk.devices().empty()) { std::printf("SKIP gpu part: no device\n"); std::puts("test_fixture_scene ok"); return 0; }
  if (!(s = vk.create_device(0))) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); std::puts("test_fixture_scene ok"); return 0; }
  { D3D11Runtime gpu; d3d11::VulkanDrawBackend be(vk); gpu.ctx->SetDrawBackend(&be);
    gpu.on_shaders_ubo = [&](const std::vector<uint32_t>& v, const std::vector<uint32_t>& p, bool vs_ubo) { be.SetShaders(v, p, vs_ubo); };
    Fake h3; User32Context u3; u3.host = &h3; ImportRegistry r3; Kernel32Context k3;
    kernel32_register(r3, k3); user32_register(r3, u3); gpu.register_imports(r3); attach_window_present(gpu, u3); CHECK(bind(r3) == 10);
    CHECK(entry() == 0 && gpu.last_error.empty() && gpu.presents == 2 && h3.presents == 2);
    CHECK(gpu.shaders_translated == 4);
  auto& im = gpu.presented;
  auto near3 = [&](const uint8_t* p, int r, int g, int b) { return std::abs(p[0] - r) <= 4 && std::abs(p[1] - g) <= 4 && std::abs(p[2] - b) <= 4; };
  CHECK(near3(px(im, 320, 300, 10), 13, 13, 38));                    // clear colour
  CHECK(near3(px(im, 320, 50, 60), 0, 255, 0));                      // near quad only
  CHECK(near3(px(im, 320, 190, 150), 255, 128, 0));                  // far quad only: shifted by the vertex cbuffer, tinted by the pixel cbuffer
  CHECK(near3(px(im, 320, 120, 100), 0, 255, 0));                    // overlap: depth keeps the near (earlier) green quad
  CHECK(near3(px(im, 320, 60, 190), 13, 13, 38));                   // below the near quad and left of the far quad: untouched
  CHECK(near3(px(im, 320, 240, 70), 255, 0, 0) && near3(px(im, 320, 290, 70), 0, 255, 0));      // texture: top row red, green
  CHECK(near3(px(im, 320, 240, 170), 0, 0, 255) && near3(px(im, 320, 290, 170), 255, 255, 255)); // bottom row blue, white
  std::printf("scene: near (%u,%u,%u) far (%u,%u,%u) overlap (%u,%u,%u)\n", px(im, 320, 50, 60)[0], px(im, 320, 50, 60)[1], px(im, 320, 50, 60)[2],
              px(im, 320, 190, 150)[0], px(im, 320, 190, 150)[1], px(im, 320, 190, 150)[2], px(im, 320, 120, 100)[0], px(im, 320, 120, 100)[1], px(im, 320, 120, 100)[2]);
  }
  std::puts("test_fixture_scene ok");
  return 0;
}
