// Third hand-assembled PE: textures, samplers and index buffers through the COM layer.
// The guest creates a render target, two 2x2 textures (one filled with UpdateSubresource, one filled by Map/Unmap on a
// dynamic texture), shader resource views, a point sampler, a vertex buffer filled by UpdateSubresource, a 16-bit index
// buffer, and draws two quads with DrawIndexed (the second uses a start index). Shaders are hand-built DXBC
// (tests/dxbc_builder.h); the translator has so far only been checked against real fxc output for the pass-through case.
// Honest framing: the PE is assembled by this test, GPU output comes from the software driver, slots come from public headers.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "dxbc_builder.h"
#include "pe_builder.h"
#include "x64_asm.h"
#include "runtime/d3d11_runtime.h"
#include "runtime/run_pe.h"
#include "gfx/d3d11_vulkan.h"
using namespace anyxbox;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
namespace {
enum { DEV = 0, CTX = 8, RT = 16, RTV = 24, TEXA = 32, TEXB = 40, SRVA = 48, SRVB = 56, SMP = 64, VB = 72, IB = 80, LAY = 88, VS = 96, PS = 104, LEVEL = 112, MAPPED = 120,
       RTDESC = 0x100, TEXADESC = 0x140, TEXBDESC = 0x180, SMPDESC = 0x1C0, VBDESC = 0x1E0, IBDESC = 0x200, IBDATA = 0x220, SUBDATA = 0x240, VP = 0x250, CLR = 0x270,
       STRIDE = 0x280, OFFSET = 0x288, ELEMS = 0x2A0, NAMES = 0x300, TEXELS = 0x320, VERTS = 0x400, VSB = 0x800, PSB = 0xC00 };
struct VT { float x, y, z, w, r, g, b, a, u, v; };
const int W = 128, H = 128;
using x64::Asm;
std::vector<uint8_t> g_vs, g_ps;

std::vector<uint8_t> build_guest() {
  Asm a;
  a.b({0x48, 0x83, 0xEC, 0x68});
  a.ptr_to_data(IBDATA, SUBDATA);   // SubData.pSysMem for the index buffer (CreateBuffer init)
  a.ptr_to_data(NAMES, ELEMS + 0); a.ptr_to_data(NAMES + 9, ELEMS + 32); a.ptr_to_data(NAMES + 15, ELEMS + 64);
  a.b({0x31, 0xC9}); a.mov_edx(1); a.zero_r8(); a.zero_r9();
  a.zero_stack(0x20); a.zero_stack(0x28); a.b({0x48, 0xC7, 0x44, 0x24, 0x30, 7, 0, 0, 0});
  a.lea(0, DEV); a.store_stack_rax(0x38); a.lea(0, LEVEL); a.store_stack_rax(0x40); a.lea(0, CTX); a.store_stack_rax(0x48);
  a.rip({0xFF, 0x15}, 0x1260); a.check();
  a.on(DEV); a.lea(2, RTDESC); a.zero_r8(); a.lea(9, RT); a.vcall(5); a.check();                   // render target texture
  a.on(DEV); a.load(2, RT); a.zero_r8(); a.lea(9, RTV); a.vcall(9); a.check();
  a.on(DEV); a.lea(2, TEXADESC); a.zero_r8(); a.lea(9, TEXA); a.vcall(5); a.check();               // texture A (default usage)
  a.on(DEV); a.lea(2, TEXBDESC); a.zero_r8(); a.lea(9, TEXB); a.vcall(5); a.check();               // texture B (dynamic)
  a.on(DEV); a.load(2, TEXA); a.zero_r8(); a.lea(9, SRVA); a.vcall(7); a.check();                  // CreateShaderResourceView
  a.on(DEV); a.load(2, TEXB); a.zero_r8(); a.lea(9, SRVB); a.vcall(7); a.check();
  a.on(DEV); a.lea(2, SMPDESC); a.lea(8, SMP); a.vcall(23); a.check();                             // CreateSamplerState
  a.on(DEV); a.lea(2, VBDESC); a.zero_r8(); a.lea(9, VB); a.vcall(3); a.check();                   // vertex buffer, no initial data
  a.on(DEV); a.lea(2, IBDESC); a.lea(8, SUBDATA); a.lea(9, IB); a.vcall(3); a.check();             // index buffer with initial data
  a.on(DEV); a.lea(2, ELEMS); a.mov_r8d(3); a.lea(9, VSB);
  a.b({0x48, 0xC7, 0x44, 0x24, 0x20}); a.d32((uint32_t)g_vs.size());
  a.lea(0, LAY); a.store_stack_rax(0x28); a.vcall(11); a.check();
  a.on(DEV); a.lea(2, VSB); a.mov_r8d((uint32_t)g_vs.size()); a.zero_r9(); a.lea(0, VS); a.store_stack_rax(0x20); a.vcall(12); a.check();
  a.on(DEV); a.lea(2, PSB); a.mov_r8d((uint32_t)g_ps.size()); a.zero_r9(); a.lea(0, PS); a.store_stack_rax(0x20); a.vcall(15); a.check();
  // UpdateSubresource(texA, 0, NULL, texels, 8, 0)  slot 48
  a.on(CTX); a.load(2, TEXA); a.zero_r8(); a.zero_r9(); a.lea(0, TEXELS); a.store_stack_rax(0x20);
  a.b({0x48, 0xC7, 0x44, 0x24, 0x28, 8, 0, 0, 0}); a.zero_stack(0x30); a.vcall(48);
  // UpdateSubresource(vb, 0, NULL, verts, 0, 0)
  a.on(CTX); a.load(2, VB); a.zero_r8(); a.zero_r9(); a.lea(0, VERTS); a.store_stack_rax(0x20); a.zero_stack(0x28); a.zero_stack(0x30); a.vcall(48);
  // Map(texB, 0, WRITE_DISCARD=4, 0, &mapped)  slot 14, fill four texels, Unmap slot 15
  a.on(CTX); a.load(2, TEXB); a.zero_r8(); a.b({0x41, 0xB9, 4, 0, 0, 0}); a.zero_stack(0x20); a.lea(0, MAPPED); a.store_stack_rax(0x28); a.vcall(14); a.check();
  a.load(0, MAPPED);
  const uint32_t texb[4] = {0xFF00FFFFu, 0xFFFFFF00u, 0xFFFF00FFu, 0xFF808080u};   // ABGR dwords: yellow, cyan, magenta, grey
  a.b({0xC7, 0x00}); a.d32(texb[0]);
  for (int i = 1; i < 4; i++) { a.b({0xC7, 0x40, i * 4}); a.d32(texb[i]); }
  a.on(CTX); a.load(2, TEXB); a.zero_r8(); a.vcall(15);
  // pipeline setup
  a.on(CTX); a.mov_edx(1); a.lea(8, RTV); a.zero_r9(); a.vcall(33);
  a.on(CTX); a.mov_edx(1); a.lea(8, VP); a.vcall(44);
  a.on(CTX); a.load(2, RTV); a.lea(8, CLR); a.vcall(50);
  a.on(CTX); a.load(2, LAY); a.vcall(17);
  a.on(CTX); a.b({0x31, 0xD2}); a.mov_r8d(1); a.lea(9, VB);
  a.lea(0, STRIDE); a.store_stack_rax(0x20); a.lea(0, OFFSET); a.store_stack_rax(0x28); a.vcall(18);
  a.on(CTX); a.load(2, IB); a.b({0x41, 0xB8, 57, 0, 0, 0}); a.zero_r9(); a.vcall(19);               // IASetIndexBuffer(ib, R16_UINT, 0)
  a.on(VB);a.vcall(2);a.on(IB);a.vcall(2); // input buffers now context-owned
  a.on(CTX); a.mov_edx(4); a.vcall(24);
  a.on(CTX); a.load(2, VS); a.zero_r8(); a.zero_r9(); a.vcall(11);
  a.on(CTX); a.load(2, PS); a.zero_r8(); a.zero_r9(); a.vcall(9);
  a.on(CTX); a.b({0x31, 0xD2}); a.mov_r8d(1); a.lea(9, SMP); a.vcall(10);                           // PSSetSamplers(0, 1, &smp)
  a.on(SMP);a.vcall(2); // caller sampler reference released after binding
  a.on(CTX); a.b({0x31, 0xD2}); a.mov_r8d(1); a.lea(9, SRVA); a.vcall(8);                           // PSSetShaderResources(0, 1, &srvA)
  a.on(SRVA);a.vcall(2);a.on(TEXA);a.vcall(2); // caller release after binding
  a.on(CTX); a.mov_edx(6); a.zero_r8(); a.zero_r9(); a.vcall(12);                                   // DrawIndexed(6, 0, 0)
  a.on(CTX); a.b({0x31, 0xD2}); a.mov_r8d(1); a.lea(9, SRVB); a.vcall(8);                           // switch to texture B
  a.on(SRVB);a.vcall(2);a.on(TEXB);a.vcall(2); // replacement view remains context-owned
  a.on(CTX); a.mov_edx(6); a.mov_r8d(6); a.zero_r9(); a.vcall(12);                                  // DrawIndexed(6, 6, 0)
  a.b({0x31, 0xC0});
  a.b({0x48, 0x83, 0xC4, 0x68, 0xC3});
  size_t fail_at = a.c.size();
  a.b({0x48, 0x83, 0xC4, 0x68, 0xC3});
  for (size_t p : a.fails) { uint32_t rel = (uint32_t)(fail_at - (p + 4)); std::memcpy(&a.c[p], &rel, 4); }
  if (a.c.size() > 0xC00) std::printf("guest code too large\n");
  auto pe = pebuild::make2({0xC3}, {"D3D11CreateDevice"}, "d3d11.dll");
  size_t o = 0x84 + 20, s1 = o + 240;
  pebuild::w32(pe, o + 16, x64::kCode); pebuild::w32(pe, s1 + 16, 0x1000);
  pe.resize(0x1200, 0);
  std::memcpy(&pe[0x600], a.c.data(), a.c.size());
  return pe;
}
void put32(std::vector<uint8_t>& d, size_t o, uint32_t v) { std::memcpy(&d[o], &v, 4); }
std::vector<uint8_t> finish_pe(std::vector<uint8_t> pe) {
  std::vector<uint8_t> data(0x1000, 0);
  // texture descs: Width Height Mips Array Format SampleCount SampleQuality Usage Bind CPUAccess Misc
  const uint32_t rt[11] = {W, H, 1, 1, 28, 1, 0, 0, 0x20 | 0x8, 0, 0}; std::memcpy(&data[RTDESC], rt, sizeof rt);
  const uint32_t ta[11] = {2, 2, 1, 1, 28, 1, 0, 0, 0x8, 0, 0}; std::memcpy(&data[TEXADESC], ta, sizeof ta);
  const uint32_t tb[11] = {2, 2, 1, 1, 28, 1, 0, 2 /*DYNAMIC*/, 0x8, 0x10000 /*CPU_ACCESS_WRITE*/, 0}; std::memcpy(&data[TEXBDESC], tb, sizeof tb);
  // sampler: MIN_MAG_MIP_POINT, CLAMP x3
  put32(data, SMPDESC + 0, 0); put32(data, SMPDESC + 4, 3); put32(data, SMPDESC + 8, 3); put32(data, SMPDESC + 12, 3);
  VT q[8]; auto quad = [&](VT* o, float x0, float x1) {
    o[0] = {x0, 1, 0, 1, 1, 1, 1, 1, 0, 0}; o[1] = {x1, 1, 0, 1, 1, 1, 1, 1, 1, 0};
    o[2] = {x0, -1, 0, 1, 1, 1, 1, 1, 0, 1}; o[3] = {x1, -1, 0, 1, 1, 1, 1, 1, 1, 1}; };
  quad(q, -1, 0); quad(q + 4, 0, 1);
  put32(data, VBDESC, sizeof q);  put32(data, VBDESC + 8, 1);
  uint16_t idx[12] = {0, 1, 2, 1, 3, 2, 4, 5, 6, 5, 7, 6};
  put32(data, IBDESC, sizeof idx); put32(data, IBDESC + 8, 2);
  std::memcpy(&data[IBDATA], idx, sizeof idx);
  const float vp[6] = {0, 0, (float)W, (float)H, 0, 1}; std::memcpy(&data[VP], vp, sizeof vp);
  const float clr[4] = {0.02f, 0.03f, 0.2f, 1}; std::memcpy(&data[CLR], clr, sizeof clr);
  put32(data, STRIDE, sizeof(VT));
  put32(data, ELEMS + 12, 2); put32(data, ELEMS + 20, 0);
  put32(data, ELEMS + 32 + 12, 2); put32(data, ELEMS + 32 + 20, 16);
  put32(data, ELEMS + 64 + 12, 16 /*R32G32_FLOAT*/); put32(data, ELEMS + 64 + 20, 32);
  std::memcpy(&data[NAMES], "POSITION", 9); std::memcpy(&data[NAMES + 9], "COLOR", 6); std::memcpy(&data[NAMES + 15], "TEXCOORD", 9);
  const uint32_t texa[4] = {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};   // red, green / blue, white
  std::memcpy(&data[TEXELS], texa, sizeof texa);
  std::memcpy(&data[VERTS], q, sizeof q);
  std::memcpy(&data[VSB], g_vs.data(), g_vs.size()); std::memcpy(&data[PSB], g_ps.data(), g_ps.size());
  size_t s2 = 0x84 + 20 + 240 + 40;
  pebuild::w32(pe, s2 + 16, 0x1000); pebuild::w32(pe, s2 + 20, 0x1200);
  pe.resize(0x1200 + 0x1000, 0);
  std::memcpy(&pe[0x1200], data.data(), data.size());
  return pe;
}
bool near_c(const uint8_t* p, int r, int g, int b, int tol = 3) { return std::abs(p[0] - r) <= tol && std::abs(p[1] - g) <= tol && std::abs(p[2] - b) <= tol; }
}  // namespace

int main() {
#if !(defined(__linux__) && defined(__x86_64__))
  std::printf("SKIP: guest code runs on Linux x86-64 only\n"); return 0;
#else
  g_vs = dxb::vs_tex_pass(); g_ps = dxb::ps_sample();
  if (g_vs.size() > 0x400 || g_ps.size() > 0x400) { std::printf("shader blobs too large for their slots\n"); return 1; }
  auto pe = finish_pe(build_guest());
  // 1. reference renderer (no GPU): the whole call sequence works and four triangles are drawn
  {
    D3D11Runtime rt;
    auto r = run_pe(pe.data(), pe.size(), true, [&](ImportRegistry& reg) { rt.register_imports(reg); });
    if (!r.loaded || !r.ran) std::printf("run failed: %s\n", r.error.c_str());
    CHECK(r.loaded && r.ran);
    std::printf("guest exit 0x%x last_error='%s' triangles_drawn=%llu\n", r.exit_code, rt.last_error.c_str(), (unsigned long long)rt.ctx->stats().triangles_drawn);
    CHECK(r.exit_code == 0 && rt.last_error.empty() && rt.shaders_translated == 2);
    CHECK(rt.ctx->stats().triangles_drawn == 4);
    std::printf("guest PE: textures, sampler, index buffers through the COM layer (reference renderer): ok\n");
  }
  // 2. Vulkan: the sampled colours must be the ones the guest uploaded
  pnr::graphics::VulkanBackend vk;
  auto s = vk.open();
  if (!s) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  s = vk.create_instance("anyxbox-test");
  if (!s || vk.devices().empty()) { std::printf("SKIP gpu part: no device\n"); return 0; }
  if (!(s = vk.create_device(0))) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  D3D11Runtime rt; d3d11::VulkanDrawBackend be(vk);
  rt.ctx->SetDrawBackend(&be);
  rt.on_shaders = [&](const std::vector<uint32_t>& v, const std::vector<uint32_t>& p) { be.SetShaders(v, p); };
  auto r = run_pe(pe.data(), pe.size(), true, [&](ImportRegistry& reg) { rt.register_imports(reg); });
  CHECK(r.ran && r.exit_code == 0);
  if (!rt.last_error.empty()) std::printf("gpu error: %s\n", rt.last_error.c_str());
  CHECK(rt.last_error.empty());
  std::vector<uint8_t> im; CHECK(rt.dev.ReadTexture(1, &im));
  auto at = [&](int x, int y) { return &im[((size_t)y * W + x) * 4]; };
  // left half = texture A (UpdateSubresource), right half = texture B (Map/Unmap); point sampling, clamp
  CHECK(near_c(at(16, 32), 255, 0, 0));   CHECK(near_c(at(48, 32), 0, 255, 0));
  CHECK(near_c(at(16, 96), 0, 0, 255));   CHECK(near_c(at(48, 96), 255, 255, 255));
  CHECK(near_c(at(80, 32), 255, 255, 0)); CHECK(near_c(at(112, 32), 0, 255, 255));
  CHECK(near_c(at(80, 96), 255, 0, 255)); CHECK(near_c(at(112, 96), 128, 128, 128));
  std::printf("guest PE sampled the uploaded textures through Vulkan: all 8 quadrants correct\n");
  std::printf("d3d11 guest textures: all ok\n");
  return 0;
#endif
}
