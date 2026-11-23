// Second hand-assembled PE: D3D11CreateDeviceAndSwapChain, swap chain back buffer as render target, a depth buffer
// with a depth-stencil view, rasterizer / depth-stencil / blend state objects, two overlapping triangles in one Draw,
// then IDXGISwapChain::Present. Shaders are the real fxc pair from tests/real_dxbc_wine.h.
// Honest framing: the PE is assembled by this test, no window is shown (Present copies the back buffer into the
// runtime), slot numbers come from the public d3d11/dxgi headers. Linux x86-64 only.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "pe_builder.h"
#include "real_dxbc_wine.h"
#include "x64_asm.h"
#include "runtime/d3d11_runtime.h"
#include "runtime/run_pe.h"
#include "gfx/d3d11_vulkan.h"
using namespace anyxbox;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
namespace {
enum { DEV = 0, CTX = 8, SWAP = 16, BB = 24, RTV = 32, DTEX = 40, DSV = 48, LAY = 56, VS = 64, PS = 72, RS = 80, DSS = 88, BLS = 96, BUF = 104, LEVEL = 112,
       SCDESC = 0x100, DEPTHDESC = 0x160, RSDESC = 0x1A0, DSDESC = 0x1E0, VP = 0x220, CLR = 0x240, ONE = 0x250, STRIDE = 0x260, OFFSET = 0x268,
       BUFDESC = 0x270, SUBDATA = 0x290, ELEMS = 0x2A0, NAMES = 0x2E0, BLDESC = 0x300, IID = 0x420, VERTS = 0x500, VSB = 0x800, PSB = 0xC00 };
struct V { float x, y, z, w, r, g, b, a; };
const int W = 128, H = 128;
using x64::Asm;

std::vector<uint8_t> build_guest() {
  Asm a;
  a.b({0x48, 0x83, 0xEC, 0x78});   // sub rsp,0x78 (keeps 16-byte alignment, room for 8 stack args)
  a.ptr_to_data(VERTS, SUBDATA); a.ptr_to_data(NAMES, ELEMS + 0); a.ptr_to_data(NAMES + 9, ELEMS + 32);
  // D3D11CreateDeviceAndSwapChain(0, HARDWARE, 0, 0, NULL, 0, 7, &scdesc, &swap, &dev, &level, &ctx)
  a.b({0x31, 0xC9}); a.mov_edx(1); a.zero_r8(); a.zero_r9();
  a.zero_stack(0x20); a.zero_stack(0x28); a.b({0x48, 0xC7, 0x44, 0x24, 0x30, 7, 0, 0, 0});
  a.lea(0, SCDESC); a.store_stack_rax(0x38); a.lea(0, SWAP); a.store_stack_rax(0x40); a.lea(0, DEV); a.store_stack_rax(0x48);
  a.lea(0, LEVEL); a.store_stack_rax(0x50); a.lea(0, CTX); a.store_stack_rax(0x58);
  a.rip({0xFF, 0x15}, 0x1260); a.check();
  // swap->GetBuffer(0, &iid, &bb)    slot 9
  a.on(SWAP); a.b({0x31, 0xD2}); a.lea(8, IID); a.lea(9, BB); a.vcall(9); a.check();
  a.on(DEV); a.load(2, BB); a.zero_r8(); a.lea(9, RTV); a.vcall(9); a.check();                     // CreateRenderTargetView
  a.on(DEV); a.lea(2, DEPTHDESC); a.zero_r8(); a.lea(9, DTEX); a.vcall(5); a.check();              // CreateTexture2D (depth)
  a.on(DEV); a.load(2, DTEX); a.zero_r8(); a.lea(9, DSV); a.vcall(10); a.check();                  // CreateDepthStencilView
  a.on(DEV); a.lea(2, RSDESC); a.lea(8, RS); a.vcall(22); a.check();                               // CreateRasterizerState
  a.on(DEV); a.lea(2, DSDESC); a.lea(8, DSS); a.vcall(21); a.check();                              // CreateDepthStencilState
  a.on(DEV); a.lea(2, BLDESC); a.lea(8, BLS); a.vcall(20); a.check();                              // CreateBlendState
  a.on(DEV); a.lea(2, BUFDESC); a.lea(8, SUBDATA); a.lea(9, BUF); a.vcall(3); a.check();           // CreateBuffer
  a.on(DEV); a.lea(2, ELEMS); a.mov_r8d(2); a.lea(9, VSB);
  a.b({0x48, 0xC7, 0x44, 0x24, 0x20}); a.d32((uint32_t)sizeof wine_vs_code);
  a.lea(0, LAY); a.store_stack_rax(0x28); a.vcall(11); a.check();                                  // CreateInputLayout
  a.on(DEV); a.lea(2, VSB); a.mov_r8d((uint32_t)sizeof wine_vs_code); a.zero_r9(); a.lea(0, VS); a.store_stack_rax(0x20); a.vcall(12); a.check();
  a.on(DEV); a.lea(2, PSB); a.mov_r8d((uint32_t)sizeof wine_ps_code); a.zero_r9(); a.lea(0, PS); a.store_stack_rax(0x20); a.vcall(15); a.check();
  // ctx->OMSetRenderTargets(1, &rtv, dsv)
  a.on(CTX); a.mov_edx(1); a.lea(8, RTV); a.load(9, DSV); a.vcall(33);
  a.on(CTX); a.mov_edx(1); a.lea(8, 0x220); a.vcall(44);                                           // RSSetViewports
  a.on(CTX); a.load(2, RTV); a.lea(8, CLR); a.vcall(50);                                           // ClearRenderTargetView
  // ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0): flags in r8d, depth in xmm3 (movss xmm3,[rip+ONE])
  a.on(CTX); a.load(2, DSV); a.mov_r8d(1); a.rip({0xF3, 0x0F, 0x10, 0x1D}, x64::kData + ONE); a.zero_stack(0x20); a.vcall(53);
  a.on(CTX); a.load(2, RS); a.vcall(43);                                                           // RSSetState
  a.on(CTX); a.load(2, DSS); a.zero_r8(); a.vcall(36);                                             // OMSetDepthStencilState
  a.on(CTX); a.load(2, BLS); a.zero_r8(); a.b({0x41, 0xB9, 0xFF, 0xFF, 0xFF, 0xFF}); a.vcall(35);  // OMSetBlendState
  a.on(CTX); a.load(2, LAY); a.vcall(17);
  a.on(CTX); a.b({0x31, 0xD2}); a.mov_r8d(1); a.lea(9, BUF);
  a.lea(0, STRIDE); a.store_stack_rax(0x20); a.lea(0, OFFSET); a.store_stack_rax(0x28); a.vcall(18);
  a.on(CTX); a.mov_edx(4); a.vcall(24);
  a.on(CTX); a.load(2, VS); a.zero_r8(); a.zero_r9(); a.vcall(11);
  a.on(CTX); a.load(2, PS); a.zero_r8(); a.zero_r9(); a.vcall(9);
  a.on(CTX); a.mov_edx(6); a.zero_r8(); a.vcall(13);                                               // Draw(6, 0)
  a.on(SWAP); a.b({0x31, 0xD2}); a.zero_r8(); a.vcall(8); a.check();                               // Present(0, 0)
  a.b({0x31, 0xC0});
  a.b({0x48, 0x83, 0xC4, 0x78, 0xC3});
  size_t fail_at = a.c.size();
  a.b({0x48, 0x83, 0xC4, 0x78, 0xC3});
  for (size_t p : a.fails) { uint32_t rel = (uint32_t)(fail_at - (p + 4)); std::memcpy(&a.c[p], &rel, 4); }
  if (a.c.size() > 0xC00) std::printf("guest code too large\n");
  auto pe = pebuild::make2({0xC3}, {"D3D11CreateDeviceAndSwapChain"}, "d3d11.dll");
  size_t o = 0x84 + 20, s1 = o + 240;
  pebuild::w32(pe, o + 16, x64::kCode); pebuild::w32(pe, s1 + 16, 0x1000);
  pe.resize(0x1200, 0);
  std::memcpy(&pe[0x600], a.c.data(), a.c.size());
  return pe;
}
void put32(std::vector<uint8_t>& d, size_t o, uint32_t v) { std::memcpy(&d[o], &v, 4); }
std::vector<uint8_t> finish_pe(std::vector<uint8_t> pe, const std::vector<V>& verts) {
  std::vector<uint8_t> data(0x1000, 0);
  // DXGI_SWAP_CHAIN_DESC: Width@0 Height@4 Format@16 (28 = RGBA8) SampleCount@28 BufferCount@40
  put32(data, SCDESC + 0, W); put32(data, SCDESC + 4, H); put32(data, SCDESC + 16, 28); put32(data, SCDESC + 28, 1); put32(data, SCDESC + 40, 1);
  // depth texture: D32_FLOAT (40), BindFlags DEPTH_STENCIL (0x40)
  const uint32_t td[11] = {W, H, 1, 1, 40, 1, 0, 0, 0x40, 0, 0}; std::memcpy(&data[DEPTHDESC], td, sizeof td);
  // rasterizer: solid fill (3), cull none (1)
  put32(data, RSDESC + 0, 3); put32(data, RSDESC + 4, 1);
  // depth-stencil: enable, write all, LESS (2)
  put32(data, DSDESC + 0, 1); put32(data, DSDESC + 4, 1); put32(data, DSDESC + 8, 2);
  // blend: RT0 disabled, defaults, write mask 0xF
  put32(data, BLDESC + 8 + 4, 2); put32(data, BLDESC + 8 + 16, 2); data[BLDESC + 8 + 28] = 0x0F;
  const float vp[6] = {0, 0, (float)W, (float)H, 0, 1}; std::memcpy(&data[VP], vp, sizeof vp);
  const float clr[4] = {0.02f, 0.03f, 0.2f, 1}; std::memcpy(&data[CLR], clr, sizeof clr);
  const float one = 1.f; std::memcpy(&data[ONE], &one, 4);
  put32(data, STRIDE, sizeof(V)); put32(data, BUFDESC, (uint32_t)(verts.size() * sizeof(V))); put32(data, BUFDESC + 8, 1);
  put32(data, ELEMS + 12, 2); put32(data, ELEMS + 20, 0); put32(data, ELEMS + 32 + 12, 2); put32(data, ELEMS + 32 + 20, 16);
  std::memcpy(&data[NAMES], "POSITION", 9); std::memcpy(&data[NAMES + 9], "COLOR", 6);
  std::memcpy(&data[VERTS], verts.data(), verts.size() * sizeof(V));
  std::memcpy(&data[VSB], wine_vs_code, sizeof wine_vs_code); std::memcpy(&data[PSB], wine_ps_code, sizeof wine_ps_code);
  size_t s2 = 0x84 + 20 + 240 + 40;
  pebuild::w32(pe, s2 + 16, 0x1000); pebuild::w32(pe, s2 + 20, 0x1200);
  pe.resize(0x1200 + 0x1000, 0);
  std::memcpy(&pe[0x1200], data.data(), data.size());
  return pe;
}
const uint8_t* px(const std::vector<uint8_t>& im, int x, int y) { return &im[((size_t)y * W + x) * 4]; }
}  // namespace

int main() {
#if !(defined(__linux__) && defined(__x86_64__))
  std::printf("SKIP: guest code runs on Linux x86-64 only\n"); return 0;
#else
  // A (red, far z=0.8) is drawn first, B (green, near z=0.2) second; sample points as in test_d3d11_state.cpp
  std::vector<V> t = {{-0.9f,-0.9f,0.8f,1, 1,0,0,1}, {0.9f,-0.9f,0.8f,1, 1,0,0,1}, {0,0.9f,0.8f,1, 1,0,0,1},
                      {-0.3f,-0.3f,0.2f,1, 0,1,0,1}, {0.9f,-0.3f,0.2f,1, 0,1,0,1}, {0.3f,0.9f,0.2f,1, 0,1,0,1}};
  auto pe = finish_pe(build_guest(), t);
  // Order-swapped copy: B first, A second. Depth must still keep B in front.
  std::vector<V> t2 = {t[3], t[4], t[5], t[0], t[1], t[2]};
  auto pe2 = finish_pe(build_guest(), t2);
  for (int variant = 0; variant < 2; variant++) {
    D3D11Runtime rt;
    auto& image = variant ? pe2 : pe;
    auto r = run_pe(image.data(), image.size(), true, [&](ImportRegistry& reg) { rt.register_imports(reg); });
    if (!r.loaded || !r.ran) std::printf("run failed: %s\n", r.error.c_str());
    CHECK(r.loaded && r.ran);
    std::printf("variant %d: exit 0x%x last_error='%s' presents=%d\n", variant, r.exit_code, rt.last_error.c_str(), rt.presents);
    CHECK(r.exit_code == 0 && rt.last_error.empty() && rt.presents == 1);
    CHECK(rt.presented_w == (uint32_t)W && rt.presented_h == (uint32_t)H);
    CHECK(rt.shaders_translated == 2);
    auto& im = rt.presented;
    CHECK(px(im, 83, 64)[1] > 200 && px(im, 83, 64)[0] < 60);    // overlap: near green wins in both draw orders
    CHECK(px(im, 45, 96)[0] > 200 && px(im, 45, 96)[1] < 60);    // A only: red
    CHECK(px(im, 2, 2)[2] > 40 && px(im, 2, 2)[0] < 20);         // clear colour
    std::printf("variant %d: depth test through COM state objects, Present: ok\n", variant);
  }
  // Vulkan: same PE, compare with the reference-renderer run
  pnr::graphics::VulkanBackend vk;
  auto s = vk.open();
  if (!s) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  s = vk.create_instance("anyxbox-test");
  if (!s || vk.devices().empty()) { std::printf("SKIP gpu part: no device\n"); return 0; }
  if (!(s = vk.create_device(0))) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  D3D11Runtime ref, gpu; d3d11::VulkanDrawBackend be(vk);
  gpu.ctx->SetDrawBackend(&be);
  gpu.on_shaders = [&](const std::vector<uint32_t>& v, const std::vector<uint32_t>& p) { be.SetShaders(v, p); };
  auto r1 = run_pe(pe.data(), pe.size(), true, [&](ImportRegistry& reg) { ref.register_imports(reg); });
  auto r2 = run_pe(pe.data(), pe.size(), true, [&](ImportRegistry& reg) { gpu.register_imports(reg); });
  CHECK(r1.ran && r2.ran && r1.exit_code == 0 && r2.exit_code == 0);
  if (!gpu.last_error.empty()) std::printf("gpu error: %s\n", gpu.last_error.c_str());
  CHECK(gpu.last_error.empty() && gpu.presents == 1);
  int bad = 0;
  for (size_t i = 0; i + 3 < ref.presented.size(); i += 4) { int d = 0; for (int k = 0; k < 3; k++) d = std::max(d, std::abs((int)ref.presented[i + k] - (int)gpu.presented[i + k])); if (d > 3) bad++; }
  std::printf("guest PE with depth + swap chain, Vulkan vs reference: mismatches %d\n", bad);
  CHECK(bad == 0);
  std::printf("d3d11 guest state: all ok\n");
  return 0;
#endif
}
