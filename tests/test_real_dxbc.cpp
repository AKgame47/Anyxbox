// Validates the DXBC reader, decoder and translator against REAL fxc output (see real_dxbc_wine.h for provenance),
// then renders with it through the D3D11 model + Vulkan and compares against the reference renderer.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "real_dxbc_wine.h"
#include "gfx/d3d11_vulkan.h"
#include "shader/dxbc_spirv.h"
using namespace anyxbox;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
struct V { float x, y, z, w, r, g, b, a; };

int main() {
  const uint8_t* vsb = (const uint8_t*)wine_vs_code; size_t vsn = sizeof wine_vs_code;
  const uint8_t* psb = (const uint8_t*)wine_ps_code; size_t psn = sizeof wine_ps_code;
  // container
  auto vc = shader::parse_dxbc(vsb, vsn), pc = shader::parse_dxbc(psb, psn);
  CHECK(vc.ok && pc.ok);
  CHECK(vc.chunk_names.size() == 3 && vc.chunk_names[0] == "ISGN" && vc.chunk_names[1] == "OSGN" && vc.chunk_names[2] == "SHDR");
  CHECK(vc.inputs.size() == 2 && vc.inputs[0].name == "SV_POSITION" && vc.inputs[1].name == "COLOR" && vc.inputs[1].reg == 1 && vc.inputs[1].mask == 0xF);
  CHECK(vc.outputs.size() == 2 && vc.outputs[0].sysval == 1 && vc.outputs[1].name == "COLOR");
  CHECK(pc.inputs.size() == 2 && pc.inputs[0].sysval == 1 && pc.outputs.size() == 1 && pc.outputs[0].name == "SV_Target");
  // decoder
  auto vp = shader::decode_shader(vc.shader, vc.shader_size), pp = shader::decode_shader(pc.shader, pc.shader_size);
  CHECK(vp.ok && vp.type == 1 && vp.major == 4 && vp.minor == 0);
  CHECK(pp.ok && pp.type == 0 && pp.major == 4 && pp.minor == 0);
  CHECK(vp.instructions.size() == 7 && pp.instructions.size() == 4);
  CHECK(vp.instructions[2].opcode == shader::OP_DCL_OUTPUT_SIV && vp.instructions[2].value == 1);
  CHECK(pp.instructions[0].opcode == shader::OP_DCL_INPUT_PS && pp.instructions[0].control == 2);   // linear interpolation
  CHECK(pp.instructions[2].opcode == shader::OP_MOV && pp.instructions[3].opcode == shader::OP_RET);
  std::printf("real fxc blobs: container + decoder ok (vs %zu instr, ps %zu instr)\n", vp.instructions.size(), pp.instructions.size());
  // the geometry shader is correctly refused, not mis-translated
  { auto r = shader::dxbc_to_spirv((const uint8_t*)wine_gs_code, sizeof wine_gs_code); CHECK(!r.ok); std::printf("geometry shader refused: %s\n", r.error.c_str()); }
  // truncations of the real blobs never crash
  for (size_t n = 0; n < vsn; n++) CHECK(!shader::dxbc_to_spirv(vsb, n).ok);
  for (size_t n = 0; n < psn; n++) CHECK(!shader::dxbc_to_spirv(psb, n).ok);
  auto vs = shader::dxbc_to_spirv(vsb, vsn), ps = shader::dxbc_to_spirv(psb, psn);
  if (!vs.ok || !ps.ok) { std::printf("translate failed: %s | %s\n", vs.error.c_str(), ps.error.c_str()); return 1; }
  std::printf("real fxc shaders translated to SPIR-V (%zu + %zu words)\n", vs.words.size(), ps.words.size());

  pnr::graphics::VulkanBackend vk;
  auto s = vk.open();
  if (!s) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  s = vk.create_instance("anyxbox-test");
  if (!s || vk.devices().empty()) { std::printf("SKIP gpu part: no device\n"); return 0; }
  if (!(s = vk.create_device(0))) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  d3d11::VulkanDrawBackend be(vk); be.SetShaders(vs.words, ps.words);
  const int W = 128, H = 128;
  const std::vector<d3d11::InputElement> lay = {{"POSITION", d3d11::Format::R32G32B32A32_FLOAT, 0}, {"COLOR", d3d11::Format::R32G32B32A32_FLOAT, 16}};
  auto mk = [&](d3d11::DrawBackend* b, d3d11::Device& dev, d3d11::DeviceContext& ctx, d3d11::ResourceId& rt) {
    rt = dev.CreateTexture2D(W, H, d3d11::Format::R8G8B8A8_UNORM); auto rtv = dev.CreateRenderTargetView(rt);
    if (b) ctx.SetDrawBackend(b);
    ctx.IASetInputLayout(lay); ctx.IASetPrimitiveTopology(d3d11::Topology::TriangleList); ctx.RSSetViewport({0, 0, (float)W, (float)H}); ctx.OMSetRenderTarget(rtv);
    const float navy[4] = {0.02f, 0.03f, 0.2f, 1}; ctx.ClearRenderTargetView(rtv, navy); };
  d3d11::Device d1, d2; d3d11::DeviceContext c1(d1), c2(d2); d3d11::ResourceId r1, r2;
  mk(&be, d1, c1, r1); mk(nullptr, d2, c2, r2);
  std::vector<V> t = {{-0.8f,-0.8f,0,1, 1,0,0,1}, {0.8f,-0.8f,0,1, 0,1,0,1}, {0,0.8f,0,1, 0,0,1,1}};
  c1.IASetVertexBuffer(d1.CreateBuffer(t.data(), t.size() * sizeof(V)), sizeof(V), 0);
  c2.IASetVertexBuffer(d2.CreateBuffer(t.data(), t.size() * sizeof(V)), sizeof(V), 0);
  CHECK(c1.Draw(3, 0) && c2.Draw(3, 0));
  std::vector<uint8_t> a, b; d1.ReadTexture(r1, &a); d2.ReadTexture(r2, &b);
  int bad = 0, cov = 0;
  for (size_t i = 0; i + 3 < a.size(); i += 4) { int d = 0; for (int k = 0; k < 3; k++) d = std::max(d, std::abs((int)a[i + k] - (int)b[i + k])); if (d > 3) bad++; if (b[i] > 60 || b[i + 1] > 60) cov++; }
  std::printf("real fxc vs+ps through Vulkan vs reference renderer: covered %d, mismatches %d\n", cov, bad);
  CHECK(bad == 0 && cov > 3000);
  std::printf("real dxbc: all ok\n");
  return 0;
}
