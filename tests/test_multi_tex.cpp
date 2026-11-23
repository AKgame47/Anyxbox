// Anyxbox: several textures in one pixel shader (t0, t2 and t1/t3 with their samplers), hand-built DXBC, through the model + Vulkan backend.
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include "dxbc_builder.h"
#include "gfx/d3d11_vulkan.h"
#include "shader/dxbc_spirv.h"
using namespace anyxbox;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
using dxb::ins; using dxb::dst; using dxb::src;
// o0 = sample(tA, sA, uv) + sample(tB, sB, uv)
static std::vector<uint8_t> ps_two(uint32_t ta, uint32_t sa, uint32_t tb, uint32_t sb) {
  std::vector<dxb::Words> code = {ins(98, {dst(dxb::T_INPUT, 1, 0x3)}, false, 2), ins(101, {dst(dxb::T_OUTPUT, 0, 0xF)}),
    dxb::dcl_tex2d(ta), dxb::dcl_tex2d(tb), dxb::dcl_sampler(sa)};
  if (sb != sa) code.push_back(dxb::dcl_sampler(sb));
  code.push_back(dxb::dcl_temps(2));
  code.push_back(ins(69, {dst(dxb::T_TEMP, 0, 0xF), src(dxb::T_INPUT, 1, dxb::swz(0, 1, 0, 0)), dxb::res_src(ta), dxb::smp_src(sa)}));
  code.push_back(ins(69, {dst(dxb::T_TEMP, 1, 0xF), src(dxb::T_INPUT, 1, dxb::swz(0, 1, 0, 0)), dxb::res_src(tb), dxb::smp_src(sb)}));
  code.push_back(ins(dxb::OP_ADD, {dst(dxb::T_OUTPUT, 0, 0xF), src(dxb::T_TEMP, 0), src(dxb::T_TEMP, 1)}));
  code.push_back(ins(dxb::OP_RET, {}));
  return dxb::shader(0, {{"TEXCOORD", 0, 0, 1, 0x3}}, {{"SV_Target", 0, 0, 0, 0xF}}, code);
}
int main() {
  auto vsb = dxb::vs_tex_pass();
  auto vsr = shader::dxbc_to_spirv(vsb.data(), vsb.size());
  CHECK(vsr.ok);
  // translator: pairing and limits
  { auto b = ps_two(0, 0, 2, 1); auto r = shader::dxbc_to_spirv(b.data(), b.size());
    CHECK(r.ok && r.tex_sampler[0] == 0 && r.tex_sampler[1] == -1 && r.tex_sampler[2] == 1 && r.tex_sampler[3] == -1);
    auto bad = ps_two(0, 0, 4, 0); r = shader::dxbc_to_spirv(bad.data(), bad.size()); CHECK(!r.ok);
    auto two = dxb::ps_sample(dxb::swz(0, 1, 2, 3), -1, 1, 0, true); r = shader::dxbc_to_spirv(two.data(), two.size()); CHECK(!r.ok);   // t1 not declared
  }
  pnr::graphics::VulkanBackend vk;
  if (!vk.open() || !vk.create_instance("anyxbox-test") || vk.devices().empty() || !vk.create_device(0)) { std::printf("SKIP gpu part\n"); return 0; }
  struct VT { float x, y, z, w, r, g, b, a, u, v; };
  const std::vector<d3d11::InputElement> lay = {{"POSITION", d3d11::Format::R32G32B32A32_FLOAT, 0}, {"COLOR", d3d11::Format::R32G32B32A32_FLOAT, 16}, {"TEXCOORD", d3d11::Format::R32G32_FLOAT, 32}};
  const int W = 32, H = 32;
  struct Case { uint32_t ta, sa, tb, sb; const char* name; };
  const Case cases[] = {{0, 0, 2, 1, "t0+t2 (s0,s1)"}, {0, 0, 1, 0, "t0+t1 (one sampler)"}, {1, 1, 3, 3, "t1+t3 with t0 also bound"}};
  for (auto& c : cases) {
    auto pb = ps_two(c.ta, c.sa, c.tb, c.sb); auto psr = shader::dxbc_to_spirv(pb.data(), pb.size()); CHECK(psr.ok);
    d3d11::VulkanDrawBackend be(vk); be.SetShaders(vsr.words, psr.words);
    d3d11::Device dev; d3d11::DeviceContext ctx(dev); ctx.SetDrawBackend(&be); ctx.SetTexturePairing(psr.tex_sampler);
    auto rt = dev.CreateTexture2D(W, H, d3d11::Format::R8G8B8A8_UNORM); auto rtv = dev.CreateRenderTargetView(rt);
    // four 2x2 textures of different, small values so the sum never saturates: texture k = (10+40k, 20+30k, 5+..., 255) uniform per quadrant pattern
    d3d11::ResourceId tex[4];
    for (int k = 0; k < 4; k++) {
      uint8_t px[2 * 2 * 4];
      for (int i = 0; i < 4; i++) { px[i * 4] = (uint8_t)(10 + 30 * k + 7 * i); px[i * 4 + 1] = (uint8_t)(5 + 20 * k); px[i * 4 + 2] = (uint8_t)(2 * i); px[i * 4 + 3] = 255; }
      tex[k] = dev.CreateTexture2D(2, 2, d3d11::Format::R8G8B8A8_UNORM); CHECK(dev.UpdateTexture(tex[k], px, sizeof px));
    }
    for (int k = 0; k < (4); k++) { CHECK(ctx.PSSetShaderResource(k, tex[k])); CHECK(ctx.PSSetSampler(k, {false, false})); }
    CHECK(!ctx.PSSetShaderResource(4, tex[0]));
    ctx.IASetInputLayout(lay); ctx.IASetPrimitiveTopology(d3d11::Topology::TriangleList); ctx.RSSetViewport({0, 0, (float)W, (float)H}); ctx.OMSetRenderTarget(rtv);
    VT a{-1, 1, 0, 1, 1, 1, 1, 1, 0, 0}, b{1, 1, 0, 1, 1, 1, 1, 1, 1, 0}, cc{-1, -1, 0, 1, 1, 1, 1, 1, 0, 1}, d{1, -1, 0, 1, 1, 1, 1, 1, 1, 1};
    std::vector<VT> q = {a, b, cc, b, d, cc};
    ctx.IASetVertexBuffer(dev.CreateBuffer(q.data(), q.size() * sizeof(VT)), sizeof(VT), 0);
    const float black[4] = {0, 0, 0, 1}; ctx.ClearRenderTargetView(rtv, black);
    if (!ctx.Draw((uint32_t)q.size(), 0)) { std::printf("%s: draw failed: %s\n", c.name, ctx.last_error().c_str()); return 1; }
    std::vector<uint8_t> img; dev.ReadTexture(rt, &img);
    int bad = 0;
    for (int y = 1; y < H; y += 8) for (int x = 1; x < W; x += 8) {
      int i = (y / 16) * 2 + (x / 16);   // texel of the 2x2 textures at this pixel (nearest)
      const uint8_t* p = &img[((size_t)y * W + x) * 4];
      int er = (10 + 30 * (int)c.ta + 7 * i) + (10 + 30 * (int)c.tb + 7 * i), eg = (5 + 20 * (int)c.ta) + (5 + 20 * (int)c.tb), eb = 4 * i;
      if (std::abs(p[0] - er) > 2 || std::abs(p[1] - eg) > 2 || std::abs(p[2] - eb) > 2) { if (!bad) std::printf("%s: px (%d,%d) got %d %d %d exp %d %d %d\n", c.name, x, y, p[0], p[1], p[2], er, eg, eb); bad++; }
    }
    std::printf("%s: %d wrong\n", c.name, bad); CHECK(bad == 0);
  }
  std::printf("multi texture: all ok\n");
  return 0;
}
