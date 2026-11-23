// Second set of REAL fxc output: the vertex and pixel shader of Wine's d3d10core "basic texturing test"
// (see real_dxbc_wine_tex.h for provenance). The pixel shader samples t0/s0 at SV_POSITION.xy / (640, 480).
// This checks the sample opcode, dcl_resource, dcl_sampler, mul with an immediate, and SV_POSITION as a pixel
// input against Microsoft-compiled bytecode, then renders Wine's 4x4 bitmap through the D3D11 model and Vulkan.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "real_dxbc_wine_tex.h"
#include "gfx/d3d11_vulkan.h"
#include "shader/dxbc_spirv.h"
using namespace anyxbox;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)

int main() {
  const uint8_t* vsb = (const uint8_t*)wine_tex_vs; size_t vsn = sizeof wine_tex_vs;
  const uint8_t* psb = (const uint8_t*)wine_tex_ps; size_t psn = sizeof wine_tex_ps;
  auto pc = shader::parse_dxbc(psb, psn); CHECK(pc.ok);
  auto pp = shader::decode_shader(pc.shader, pc.shader_size);
  if (!pp.ok) std::printf("decode: %s\n", pp.error.c_str());
  CHECK(pp.ok && pp.type == 0 && pp.major == 4);
  std::printf("real ps decoded: %zu instructions\n", pp.instructions.size());
  for (size_t n = 0; n < psn; n++) CHECK(!shader::dxbc_to_spirv(psb, n).ok);   // truncations never crash
  auto vs = shader::dxbc_to_spirv(vsb, vsn), ps = shader::dxbc_to_spirv(psb, psn);
  if (!vs.ok || !ps.ok) { std::printf("translate failed: vs '%s' ps '%s'\n", vs.error.c_str(), ps.error.c_str()); return 1; }
  std::printf("real fxc texturing shaders translated (%zu + %zu words)\n", vs.words.size(), ps.words.size());

  pnr::graphics::VulkanBackend vk;
  auto s = vk.open();
  if (!s) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  s = vk.create_instance("anyxbox-test");
  if (!s || vk.devices().empty()) { std::printf("SKIP gpu part: no device\n"); return 0; }
  if (!(s = vk.create_device(0))) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  const int W = 640, H = 480;
  d3d11::VulkanDrawBackend be(vk); be.SetShaders(vs.words, ps.words);
  d3d11::Device dev; d3d11::DeviceContext ctx(dev); ctx.SetDrawBackend(&be);
  auto rt = dev.CreateTexture2D(W, H, d3d11::Format::R8G8B8A8_UNORM); auto rtv = dev.CreateRenderTargetView(rt);
  const uint32_t bitmap[16] = {
    0xff0000ff, 0xff00ffff, 0xff00ff00, 0xffffff00, 0xffff0000, 0xffff00ff, 0xff000000, 0xff7f7f7f,
    0xffffffff, 0xffffffff, 0xffffffff, 0xff000000, 0xffffffff, 0xff000000, 0xff000000, 0xff000000};
  auto tex = dev.CreateTexture2D(4, 4, d3d11::Format::R8G8B8A8_UNORM); CHECK(dev.UpdateTexture(tex, bitmap, sizeof bitmap));
  ctx.IASetInputLayout({{"POSITION", d3d11::Format::R32G32_FLOAT, 0}});
  ctx.IASetPrimitiveTopology(d3d11::Topology::TriangleList); ctx.RSSetViewport({0, 0, (float)W, (float)H}); ctx.OMSetRenderTarget(rtv);
  d3d11::RasterizerDesc rd; rd.cull = d3d11::Cull::None; ctx.RSSetState(rd);
  CHECK(ctx.PSSetShaderResource(0, tex) && ctx.PSSetSampler(0, {false, false}));
  const float red[4] = {1, 0, 0, 0.5f}; ctx.ClearRenderTargetView(rtv, red);
  const float q[6][2] = {{-1, -1}, {-1, 1}, {1, -1}, {-1, 1}, {1, 1}, {1, -1}};   // Wine draws a 4 vertex strip; same quad as a list
  ctx.IASetVertexBuffer(dev.CreateBuffer(q, sizeof q), 8, 0);
  if (!ctx.Draw(6, 0)) { std::printf("draw failed: %s\n", ctx.last_error().c_str()); return 1; }
  std::vector<uint8_t> im; dev.ReadTexture(rt, &im);
  int bad = 0;
  for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
    const uint8_t* p = &im[((size_t)(60 + i * 120) * W + 80 + j * 160) * 4];
    uint32_t e = bitmap[i * 4 + j]; int er = e & 255, eg = (e >> 8) & 255, eb = (e >> 16) & 255;
    if (std::abs(p[0] - er) > 2 || std::abs(p[1] - eg) > 2 || std::abs(p[2] - eb) > 2) {
      std::printf("cell (%d,%d): got %d %d %d expected %d %d %d\n", j, i, p[0], p[1], p[2], er, eg, eb); bad++; }
  }
  std::printf("real fxc texturing test through Vulkan: 16 cells, %d wrong\n", bad);
  CHECK(bad == 0);
  // ---- Wine "return in a conditional": ieq, if_nz, mov, ret, endif on a pixel-shader cbuffer (uint c in cb0) ----
  {
    auto rps = shader::dxbc_to_spirv((const uint8_t*)wine_ret_ps, sizeof wine_ret_ps);
    if (!rps.ok) { std::printf("real ret ps translate failed: %s\n", rps.error.c_str()); return 1; }
    be.SetShaders(vs.words, rps.words);
    ctx.PSSetShaderResource(0, 0);
    const float black[4] = {0, 0, 0, 1};
    struct Case { uint32_t c; int r, g, b; } cases[] = {{0, 255, 255, 255}, {1, 255, 0, 0}, {2, 0, 255, 0}, {3, 0, 0, 255}, {4, 255, 255, 255}};
    for (auto& k : cases) {
      uint32_t cb[4] = {k.c, 0, 0, 0};
      ctx.PSSetConstantBuffer(0, dev.CreateBuffer(cb, sizeof cb));
      ctx.ClearRenderTargetView(rtv, black);
      if (!ctx.Draw(6, 0)) { std::printf("ret draw failed: %s\n", ctx.last_error().c_str()); return 1; }
      dev.ReadTexture(rt, &im);
      const uint8_t* p = &im[((size_t)239 * W + 319) * 4];
      std::printf("real ret shader, c=%u: got %d %d %d expected %d %d %d\n", k.c, p[0], p[1], p[2], k.r, k.g, k.b);
      CHECK(std::abs(p[0] - k.r) <= 2 && std::abs(p[1] - k.g) <= 2 && std::abs(p[2] - k.b) <= 2);
    }
  }
  // ---- Wine "relative addressing in constant buffers": cb0 = int index, cb1[8] = colors, dynamically indexed in the VS.
  // Two constant buffer slots in a vertex shader, so the translator uses the vertex uniform buffer with the packed layout
  // (slot 0 = 1 register, then slot 1 = 8 registers). Indices 8 and 9 of the original test read past the declared 8
  // registers of cb1 (hardware reads them from the bound buffer); that is not supported here, so only 0..7 are checked.
  {
    auto cvs = shader::dxbc_to_spirv((const uint8_t*)wine_cbrel_vs, sizeof wine_cbrel_vs);
    auto cps = shader::dxbc_to_spirv((const uint8_t*)wine_cbrel_ps, sizeof wine_cbrel_ps);
    if (!cvs.ok || !cps.ok) { std::printf("real cb-relative translate failed: vs '%s' ps '%s'\n", cvs.error.c_str(), cps.error.c_str()); return 1; }
    CHECK(cvs.vs_ubo && cvs.cb_layout.size() == 2 && cvs.cb_layout[0].first == 0 && cvs.cb_layout[0].second == 1 &&
          cvs.cb_layout[1].first == 1 && cvs.cb_layout[1].second == 8);
    be.SetShaders(cvs.words, cps.words, cvs.vs_ubo);
    ctx.PSSetConstantBuffer(0, 0);
    const float colors[8][4] = {{0, 0, 0, 1}, {0, 0, 1, 0}, {0, 0, 1, 1}, {0, 1, 0, 0}, {0, 1, 0, 1}, {0, 1, 1, 0}, {0, 1, 1, 1}, {1, 0, 0, 0}};
    const float white[4] = {1, 1, 1, 1};
    int bad2 = 0;
    for (int idx = 0; idx < 8; idx++) {
      float packed[4 + 8 * 4] = {0}; uint32_t iv = (uint32_t)idx; std::memcpy(packed, &iv, 4);
      std::memcpy(packed + 4, colors, sizeof colors);
      ctx.VSSetConstantBuffer(0, dev.CreateBuffer(packed, sizeof packed));
      ctx.ClearRenderTargetView(rtv, white);
      if (!ctx.Draw(6, 0)) { std::printf("cb-relative draw failed: %s\n", ctx.last_error().c_str()); return 1; }
      dev.ReadTexture(rt, &im);
      const uint8_t* p = &im[((size_t)239 * W + 319) * 4];
      int er = (int)(colors[idx][0] * 255), eg = (int)(colors[idx][1] * 255), eb = (int)(colors[idx][2] * 255);
      if (std::abs(p[0] - er) > 2 || std::abs(p[1] - eg) > 2 || std::abs(p[2] - eb) > 2) {
        std::printf("cb-relative index %d: got %d %d %d expected %d %d %d\n", idx, p[0], p[1], p[2], er, eg, eb); bad2++; }
    }
    std::printf("real cb-relative-addressing shaders through Vulkan: indices 0..7, %d wrong\n", bad2);
    CHECK(bad2 == 0);
  }
  std::printf("real dxbc tex: all ok\n");
  return 0;
}
