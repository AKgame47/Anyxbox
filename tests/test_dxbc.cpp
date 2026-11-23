// DXBC container + SM4 decoder + SPIR-V translation tests. Inputs are hand-built (tests/dxbc_builder.h), never compiler output.
// The end-to-end part renders through the D3D11 model + Vulkan backend with translated shaders and compares with the CPU
// reference renderer plus the shader math evaluated on the CPU. Skips the GPU part (exit 0) when no Vulkan device exists.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "dxbc_builder.h"
#include "gfx/d3d11_vulkan.h"
#include "shader/dxbc_spirv.h"
using namespace anyxbox;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
using shader::dxbc_to_spirv;

static const int W = 128, H = 128;
static const std::vector<d3d11::InputElement> kLayout = {{"POSITION", d3d11::Format::R32G32B32A32_FLOAT, 0}, {"COLOR", d3d11::Format::R32G32B32A32_FLOAT, 16}};
struct V { float x, y, z, w, r, g, b, a; };
struct Rig {
  d3d11::Device dev; d3d11::DeviceContext ctx; d3d11::ResourceId tex = 0, rtv = 0;
  explicit Rig(d3d11::DrawBackend* be = nullptr) : ctx(dev) {
    tex = dev.CreateTexture2D(W, H, d3d11::Format::R8G8B8A8_UNORM); rtv = dev.CreateRenderTargetView(tex);
    if (be) ctx.SetDrawBackend(be);
    ctx.IASetInputLayout(kLayout); ctx.IASetPrimitiveTopology(d3d11::Topology::TriangleList);
    ctx.RSSetViewport({0, 0, (float)W, (float)H}); ctx.OMSetRenderTarget(rtv);
  }
  bool draw(const std::vector<V>& v) {
    ctx.IASetVertexBuffer(dev.CreateBuffer(v.data(), v.size() * sizeof(V)), sizeof(V), 0);
    return ctx.Draw((uint32_t)v.size(), 0);
  }
  std::vector<uint8_t> image() { std::vector<uint8_t> o; dev.ReadTexture(tex, &o); return o; }
};
static float sat(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

// Render the same triangle (scaled by 0.5 in xy by the vertex shader) with translated shaders, and the pre-scaled one on the
// reference path; apply f on the CPU to covered reference pixels; return number of pixels off by more than tol.
template <class F> static int e2e(pnr::graphics::VulkanBackend& vk, const std::vector<uint8_t>& vsb, const std::vector<uint8_t>& psb, F f, int tol, int* covered) {
  auto vs = dxbc_to_spirv(vsb.data(), vsb.size()), ps = dxbc_to_spirv(psb.data(), psb.size());
  if (!vs.ok || !ps.ok) { std::printf("translate failed: %s | %s\n", vs.error.c_str(), ps.error.c_str()); return -1; }
  d3d11::VulkanDrawBackend be(vk); be.SetShaders(vs.words, ps.words);
  const float navy[4] = {0.02f, 0.03f, 0.2f, 1};
  std::vector<V> big = {{-1.6f,-1.6f,0,1, 1,0,0,1}, {1.6f,-1.6f,0,1, 0,1,0,1}, {0,1.6f,0,1, 0.2f,0.4f,1,1}};
  std::vector<V> small = big; for (auto& v : small) { v.x *= 0.5f; v.y *= 0.5f; }
  Rig gpu(&be), ref;
  gpu.ctx.ClearRenderTargetView(gpu.rtv, navy); ref.ctx.ClearRenderTargetView(ref.rtv, navy);
  if (!gpu.draw(big)) { std::printf("gpu draw failed\n"); return -1; }
  if (!ref.draw(small)) return -1;
  auto a = gpu.image(), b = ref.image();
  int bad = 0; *covered = 0;
  std::vector<uint8_t> clr(ref.image().size());
  Rig blank; blank.ctx.ClearRenderTargetView(blank.rtv, navy); auto c = blank.image();
  for (size_t i = 0; i + 3 < a.size(); i += 4) {
    bool cov = b[i] != c[i] || b[i + 1] != c[i + 1] || b[i + 2] != c[i + 2];
    int exp[3] = {c[i], c[i + 1], c[i + 2]};
    if (cov) { (*covered)++; float o[3]; f(b[i] / 255.f, b[i + 1] / 255.f, b[i + 2] / 255.f, o); for (int k = 0; k < 3; k++) exp[k] = (int)std::lround(sat(o[k]) * 255.f); }
    int d = 0; for (int k = 0; k < 3; k++) d = std::max(d, std::abs((int)a[i + k] - exp[k]));
    if (d > tol) bad++;
  }
  return bad;
}

int main() {
  const auto vsb = dxb::vs_scale_half(), psb = dxb::ps_luma();

  // ---- container ----
  { auto c = shader::parse_dxbc(vsb.data(), vsb.size());
    CHECK(c.ok); CHECK(c.chunk_names.size() == 3 && c.chunk_names[0] == "ISGN" && c.chunk_names[2] == "SHDR");
    CHECK(c.inputs.size() == 2 && c.inputs[0].name == "POSITION" && c.inputs[1].name == "COLOR" && c.inputs[1].reg == 1 && c.inputs[1].mask == 7);
    CHECK(c.outputs.size() == 2 && c.outputs[0].name == "SV_Position" && c.outputs[0].sysval == 1);
    CHECK(!c.checksum_verified);
    auto p = shader::decode_shader(c.shader, c.shader_size);
    CHECK(p.ok && p.type == 1 && p.major == 4 && p.minor == 0);
    CHECK(p.instructions.size() == 10);
    CHECK(p.instructions[5].opcode == shader::OP_MUL && p.instructions[5].ops[2].type == shader::OT_IMM32);
    CHECK(p.instructions.back().opcode == shader::OP_RET);
    // known token encodings (from the SM4 spec) match what the builder emits
    CHECK(dxb::dst(dxb::T_OUTPUT, 0, 0xF)[0] == 0x001020F2u);
    CHECK(dxb::src(dxb::T_INPUT, 0)[0] == 0x00101E46u);
    CHECK(dxb::ins(dxb::OP_MOV, {dxb::dst(dxb::T_OUTPUT, 0, 0xF), dxb::src(dxb::T_INPUT, 0)})[0] == 0x05000036u);
    CHECK(dxb::ins(98, {dxb::dst(dxb::T_INPUT, 1, 0xF)}, false, 2)[0] == 0x03001062u); }

  // ---- container error paths: every truncation, bad fields, and every single-byte corruption must be handled without a crash ----
  { for (size_t n = 0; n < vsb.size(); n++) { auto r = dxbc_to_spirv(vsb.data(), n); CHECK(!r.ok); }
    std::vector<uint8_t> m = vsb; m[0] = 'X'; CHECK(!dxbc_to_spirv(m.data(), m.size()).ok);
    m = vsb; m[24] ^= 1; CHECK(!dxbc_to_spirv(m.data(), m.size()).ok);              // size field
    m = vsb; m[28] = 200; CHECK(!dxbc_to_spirv(m.data(), m.size()).ok);              // chunk count
    m = vsb; m[32] = 0xF0; m[33] = 0xFF; CHECK(!dxbc_to_spirv(m.data(), m.size()).ok); // chunk offset
    int ok_count = 0, bad_count = 0;
    for (const auto* base : {&vsb, &psb}) for (size_t i = 0; i < base->size(); i++) for (int bit : {0, 7, 24, 31}) {
      std::vector<uint8_t> x = *base; x[i] ^= (uint8_t)(1u << (bit % 8));
      auto r = dxbc_to_spirv(x.data(), x.size()); (r.ok ? ok_count : bad_count)++;
      if (r.ok) CHECK(!r.words.empty() && r.words[0] == 0x07230203); else CHECK(!r.error.empty());
    }
    std::printf("corruption sweep: %d translated, %d rejected\n", ok_count, bad_count); CHECK(bad_count > 0); }

  // ---- decoder / translator rejection of unsupported content ----
  { // unknown opcode (30 is not in the table)
    auto bad = dxb::shader(1, {}, {{"SV_Position", 0, 1, 0, 0xF}}, {dxb::ins(103, {dxb::dst(dxb::T_OUTPUT, 0, 0xF)}, false, 0, {1}), dxb::ins(30, {}), dxb::ins(dxb::OP_RET, {})});
    auto r = dxbc_to_spirv(bad.data(), bad.size()); CHECK(!r.ok && r.error.find("opcode 30") != std::string::npos);
    // constant buffer declaration
    auto cb = dxb::shader(1, {}, {{"SV_Position", 0, 1, 0, 0xF}}, {dxb::ins(89, {dxb::dst(dxb::T_CB, 0, 0xF)}), dxb::ins(dxb::OP_RET, {})});
    r = dxbc_to_spirv(cb.data(), cb.size()); CHECK(!r.ok);
    // geometry stage
    auto gs = dxb::shader(2, {}, {}, {dxb::ins(dxb::OP_RET, {})}); r = dxbc_to_spirv(gs.data(), gs.size()); CHECK(!r.ok);
    // missing ret
    auto nr = dxb::shader(1, {}, {{"SV_Position", 0, 1, 0, 0xF}}, {dxb::ins(103, {dxb::dst(dxb::T_OUTPUT, 0, 0xF)}, false, 0, {1})});
    r = dxbc_to_spirv(nr.data(), nr.size()); CHECK(!r.ok);
    // read of an undeclared temp
    auto ut = dxb::shader(1, {}, {{"SV_Position", 0, 1, 0, 0xF}}, {dxb::ins(103, {dxb::dst(dxb::T_OUTPUT, 0, 0xF)}, false, 0, {1}),
                dxb::ins(dxb::OP_MOV, {dxb::dst(dxb::T_OUTPUT, 0, 0xF), dxb::src(dxb::T_TEMP, 3)}), dxb::ins(dxb::OP_RET, {})});
    r = dxbc_to_spirv(ut.data(), ut.size()); CHECK(!r.ok && r.error.find("temporary") != std::string::npos);
    // shader length token wrong
    auto c = shader::parse_dxbc(vsb.data(), vsb.size()); std::vector<uint8_t> m = vsb; m[(c.shader - vsb.data()) + 4] += 4;
    r = dxbc_to_spirv(m.data(), m.size()); CHECK(!r.ok && r.error.find("length") != std::string::npos);
    std::printf("rejection paths ok\n"); }

  // ---- constant buffer rejection paths ----
  { auto ok = dxb::vs_cb_scale_offset(); auto r = dxbc_to_spirv(ok.data(), ok.size()); CHECK(r.ok && r.words.size() > 50);
    auto bad = dxb::vs_cb_scale_offset(2, 2);  r = dxbc_to_spirv(bad.data(), bad.size()); CHECK(!r.ok && r.error.find("past the declared") != std::string::npos);
    bad = dxb::vs_cb_scale_offset(9, 1);       r = dxbc_to_spirv(bad.data(), bad.size()); CHECK(r.ok && r.vs_ubo);   // 9 registers no longer fit push constants: vertex uniform buffer path
    bad = dxb::vs_cb_scale_offset(2, 1, true); r = dxbc_to_spirv(bad.data(), bad.size()); CHECK(r.ok);   // dynamically indexed declaration is accepted now
    bad = dxb::vs_cb_scale_offset(2, 1, false, 1); r = dxbc_to_spirv(bad.data(), bad.size()); CHECK(r.ok && r.vs_ubo && r.cb_layout.size() == 1 && r.cb_layout[0].first == 1);   // slot 1 alone: uniform buffer path
    { auto pc = dxb::ps_cb_tint(); r = dxbc_to_spirv(pc.data(), pc.size()); CHECK(r.ok); auto pc2 = dxb::ps_cb_tint(1025); r = dxbc_to_spirv(pc2.data(), pc2.size()); CHECK(!r.ok && r.error.find("1024") != std::string::npos); }
    auto pcb = dxb::shader(0, {}, {{"SV_Target", 0, 0, 0, 0xF}}, {dxb::dcl_cb(0, 1), dxb::ins(101, {dxb::dst(dxb::T_OUTPUT, 0, 0xF)}),
                 dxb::ins(dxb::OP_MOV, {dxb::dst(dxb::T_OUTPUT, 0, 0xF), dxb::src_cb(0, 0)}), dxb::ins(dxb::OP_RET, {})});
    r = dxbc_to_spirv(pcb.data(), pcb.size()); CHECK(r.ok);   // cb0[0] of a declared cb0[1] in a pixel shader is valid now
    for (size_t n = 0; n < ok.size(); n++) CHECK(!dxbc_to_spirv(ok.data(), n).ok);
    for (size_t i = 0; i < ok.size(); i++) for (int bit = 0; bit < 8; bit++) { auto x = ok; x[i] ^= (uint8_t)(1u << bit); (void)dxbc_to_spirv(x.data(), x.size()); }
    d3d11::Device dv; d3d11::DeviceContext cx(dv); auto buf = dv.CreateBuffer("abcd", 4);
    CHECK(cx.VSSetConstantBuffer(1, buf)); CHECK(!cx.VSSetConstantBuffer(14, buf)); CHECK(!cx.VSSetConstantBuffer(0, 999)); CHECK(cx.VSSetConstantBuffer(0, buf)); CHECK(cx.VSSetConstantBuffer(0, 0));
    std::printf("constant buffer rejection paths ok\n"); }

  // ---- texture translation rejection paths ----
  { auto ok = dxb::ps_sample(); auto r = dxbc_to_spirv(ok.data(), ok.size()); CHECK(r.ok);
    auto lod = dxb::ps_sample(dxb::swz(2, 1, 0, 3), 0.0f); r = dxbc_to_spirv(lod.data(), lod.size()); CHECK(r.ok);
    auto t1 = dxb::ps_sample(dxb::swz(0, 1, 2, 3), -1, 1, 0); r = dxbc_to_spirv(t1.data(), t1.size()); CHECK(!r.ok && r.error.find("declared") != std::string::npos);
    auto s1 = dxb::ps_sample(dxb::swz(0, 1, 2, 3), -1, 0, 1); r = dxbc_to_spirv(s1.data(), s1.size()); CHECK(!r.ok && r.error.find("declared") != std::string::npos);
    auto nd = dxb::ps_sample(dxb::swz(0, 1, 2, 3), -1, 0, 0, false); r = dxbc_to_spirv(nd.data(), nd.size()); CHECK(!r.ok && r.error.find("declared") != std::string::npos);
    auto vt = dxb::shader(1, {}, {{"SV_Position", 0, 1, 0, 0xF}}, {dxb::ins(103, {dxb::dst(dxb::T_OUTPUT, 0, 0xF)}, false, 0, {1}), dxb::dcl_tex2d(0), dxb::ins(dxb::OP_RET, {})});
    r = dxbc_to_spirv(vt.data(), vt.size()); CHECK(!r.ok && r.error.find("vertex") != std::string::npos);
    auto half = dxb::shader(0, {}, {{"SV_Target", 0, 0, 0, 0xF}}, {dxb::ins(101, {dxb::dst(dxb::T_OUTPUT, 0, 0xF)}), dxb::dcl_tex2d(0), dxb::ins(dxb::OP_MOV, {dxb::dst(dxb::T_OUTPUT, 0, 0xF), dxb::imm4(0, 0, 0, 1)}), dxb::ins(dxb::OP_RET, {})});
    r = dxbc_to_spirv(half.data(), half.size()); CHECK(!r.ok && r.error.find("together") != std::string::npos);
    auto badret = dxb::shader(0, {}, {{"SV_Target", 0, 0, 0, 0xF}}, {dxb::ins(101, {dxb::dst(dxb::T_OUTPUT, 0, 0xF)}), dxb::dcl_tex2d(0, 0x4444), dxb::dcl_sampler(0), dxb::ins(dxb::OP_RET, {})});
    r = dxbc_to_spirv(badret.data(), badret.size()); CHECK(!r.ok && r.error.find("float4") != std::string::npos);
    auto cmp = dxb::shader(0, {}, {{"SV_Target", 0, 0, 0, 0xF}}, {dxb::ins(101, {dxb::dst(dxb::T_OUTPUT, 0, 0xF)}), dxb::dcl_tex2d(0), dxb::dcl_sampler(0, 1), dxb::ins(dxb::OP_RET, {})});
    r = dxbc_to_spirv(cmp.data(), cmp.size()); CHECK(!r.ok && r.error.find("comparison") != std::string::npos);
    for (size_t n = 0; n < ok.size(); n++) CHECK(!dxbc_to_spirv(ok.data(), n).ok);
    for (size_t i = 0; i < ok.size(); i++) for (int bit = 0; bit < 8; bit++) { auto x = ok; x[i] ^= (uint8_t)(1u << bit); (void)dxbc_to_spirv(x.data(), x.size()); }
    d3d11::Device dv; d3d11::DeviceContext cx(dv); auto tx = dv.CreateTexture2D(2, 2, d3d11::Format::R8G8B8A8_UNORM);
    uint8_t px[16] = {0}; CHECK(dv.UpdateTexture(tx, px, 16)); CHECK(!dv.UpdateTexture(tx, px, 15)); CHECK(!dv.UpdateTexture(77, px, 16));
    CHECK(cx.PSSetShaderResource(1, tx) && !cx.PSSetShaderResource(4, tx)); CHECK(!cx.PSSetShaderResource(0, 77)); CHECK(cx.PSSetShaderResource(0, tx)); CHECK(cx.PSSetSampler(2, {})); CHECK(!cx.PSSetSampler(4, {}));
    std::printf("texture rejection paths ok\n"); }

  // ---- control flow translation and rejection ----
  { for (auto& sh : {dxb::ps_if_else(true), dxb::ps_if_else(false), dxb::ps_loop(false), dxb::ps_loop(true), dxb::ps_discard(), dxb::ps_int_movc(), dxb::ps_pass()}) {
      auto r = dxbc_to_spirv(sh.data(), sh.size()); CHECK(r.ok);
      for (size_t n = 0; n < sh.size(); n++) CHECK(!dxbc_to_spirv(sh.data(), n).ok);
      for (size_t i = 0; i < sh.size(); i++) for (int bit = 0; bit < 8; bit++) { auto x = sh; x[i] ^= (uint8_t)(1u << bit); (void)dxbc_to_spirv(x.data(), x.size()); }
    }
    { auto v = dxb::vs_early_ret(); auto r = dxbc_to_spirv(v.data(), v.size()); CHECK(r.ok); }
    auto bad_flow = [&](std::vector<dxb::Words> body, const char* what, const char* needle) -> int {
      auto h = dxb::ps_head(1); h.insert(h.end(), body.begin(), body.end()); h.push_back(dxb::ins(dxb::OP_RET, {}));
      auto sh = dxb::shader(0, {{"COLOR", 0, 0, 1, 0xF}}, {{"SV_Target", 0, 0, 0, 0xF}}, h);
      auto r = dxbc_to_spirv(sh.data(), sh.size());
      if (r.ok || r.error.find(needle) == std::string::npos) { std::printf("flow case '%s' not rejected as expected (ok=%d, error=%s)\n", what, (int)r.ok, r.error.c_str()); return 1; }
      return 0; };
    CHECK(!bad_flow({dxb::ins(dxb::OP_ELSE, {})}, "else alone", "else"));
    CHECK(!bad_flow({dxb::ins(dxb::OP_ENDIF, {})}, "endif alone", "endif"));
    CHECK(!bad_flow({dxb::ins(dxb::OP_ENDLOOP, {})}, "endloop alone", "endloop"));
    CHECK(!bad_flow({dxb::ins(dxb::OP_BREAK, {})}, "break outside loop", "outside a loop"));
    CHECK(!bad_flow({dxb::ins(dxb::OP_LOOP, {})}, "unterminated loop", "unterminated"));
    CHECK(!bad_flow({dxb::ins(dxb::OP_IF, {dxb::imm1u(1)}, false, dxb::NZ)}, "unterminated if", "unterminated"));
    { auto h = std::vector<dxb::Words>{dxb::ins(101, {dxb::dst(dxb::T_OUTPUT, 0, 0xF)}), dxb::ins(dxb::OP_MOV, {dxb::dst(dxb::T_OUTPUT, 0, 0xF), dxb::imm4(0, 0, 0, 1)})};
      auto sh = dxb::shader(0, {}, {{"SV_Target", 0, 0, 0, 0xF}}, h); auto r = dxbc_to_spirv(sh.data(), sh.size()); CHECK(!r.ok && r.error.find("end with ret") != std::string::npos); }
    { auto vd = dxb::shader(1, {}, {{"SV_Position", 0, 1, 0, 0xF}}, {dxb::ins(103, {dxb::dst(dxb::T_OUTPUT, 0, 0xF)}, false, 0, {1}), dxb::ins(dxb::OP_DISCARD, {dxb::imm1u(1)}, false, dxb::NZ), dxb::ins(dxb::OP_RET, {})});
      auto r = dxbc_to_spirv(vd.data(), vd.size()); CHECK(!r.ok && r.error.find("discard") != std::string::npos); }
    std::printf("control flow translation and rejection ok\n"); }

  // ---- translation output shape ----
  { auto r = dxbc_to_spirv(vsb.data(), vsb.size()); CHECK(r.ok && !r.is_pixel && r.words.size() > 40 && r.words[0] == 0x07230203);
    auto p = dxbc_to_spirv(psb.data(), psb.size()); CHECK(p.ok && p.is_pixel);
    CHECK(r.words[3] > 1 && r.words[3] < 400);   // id bound sane
    std::printf("spirv words: vs %zu, ps %zu\n", r.words.size(), p.words.size()); }

  // ---- GPU end to end ----
  pnr::graphics::VulkanBackend vk;
  auto s = vk.open();
  if (!s) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  s = vk.create_instance("anyxbox-test");
  if (!s || vk.devices().empty()) { std::printf("SKIP gpu part: no device\n"); return 0; }
  if (!(s = vk.create_device(0))) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  std::printf("device: %s\n", vk.device_name().c_str());
  int cov = 0, bad;
  bad = e2e(vk, vsb, psb, [](float r, float g, float b, float* o) { float y = 0.5f * (0.299f * r + 0.587f * g + 0.114f * b) + 0.25f; o[0] = o[1] = o[2] = y; }, 3, &cov);
  std::printf("luma shader: covered %d, mismatches %d\n", cov, bad); CHECK(bad == 0 && cov > 1500);
  bad = e2e(vk, vsb, dxb::ps_sat_affine(), [](float r, float g, float b, float* o) { o[0] = 2 * r - 0.5f; o[1] = 2 * g - 0.5f; o[2] = 2 * b - 0.5f; }, 4, &cov);
  std::printf("saturate shader: covered %d, mismatches %d\n", cov, bad); CHECK(bad == 0 && cov > 1500);
  bad = e2e(vk, vsb, dxb::ps_negabs(), [](float r, float g, float b, float* o) { o[0] = r; o[1] = g; o[2] = b; }, 3, &cov);
  std::printf("neg/abs shader: covered %d, mismatches %d\n", cov, bad); CHECK(bad == 0 && cov > 1500);
  // constant buffer: pos = v0 * cb0[0] + cb0[1], changed between draws
  for (int variant = 0; variant < 2; variant++) {
    float cb[8] = {0.5f, 0.5f, 1, 1,  0.25f, -0.25f, 0, 0};
    if (variant) { cb[0] = 0.3f; cb[1] = 0.6f; cb[4] = -0.3f; cb[5] = 0.2f; }
    auto vcb = dxb::vs_cb_scale_offset(); auto vsr = dxbc_to_spirv(vcb.data(), vcb.size()), psr = dxbc_to_spirv(psb.data(), psb.size());
    CHECK(vsr.ok && psr.ok);
    d3d11::VulkanDrawBackend be(vk); be.SetShaders(vsr.words, psr.words);
    const float navy[4] = {0.02f, 0.03f, 0.2f, 1};
    std::vector<V> big = {{-1.6f,-1.6f,0,1, 1,0,0,1}, {1.6f,-1.6f,0,1, 0,1,0,1}, {0,1.6f,0,1, 0.2f,0.4f,1,1}};
    std::vector<V> moved = big; for (auto& v : moved) { v.x = v.x * cb[0] + cb[4]; v.y = v.y * cb[1] + cb[5]; }
    Rig gpu(&be), ref;
    CHECK(gpu.ctx.VSSetConstantBuffer(0, gpu.dev.CreateBuffer(cb, sizeof cb)));
    gpu.ctx.ClearRenderTargetView(gpu.rtv, navy); ref.ctx.ClearRenderTargetView(ref.rtv, navy);
    CHECK(gpu.draw(big) && ref.draw(moved));
    auto a = gpu.image(), b = ref.image(); Rig blank; blank.ctx.ClearRenderTargetView(blank.rtv, navy); auto c = blank.image();
    int bad2 = 0, cov2 = 0;
    for (size_t i = 0; i + 3 < a.size(); i += 4) {
      bool cv = b[i] != c[i] || b[i + 1] != c[i + 1] || b[i + 2] != c[i + 2]; if (cv) cov2++;
      int exp[3]; for (int k = 0; k < 3; k++) exp[k] = cv ? (int)std::lround(sat(0.5f * (0.299f * b[i] + 0.587f * b[i + 1] + 0.114f * b[i + 2]) / 255.f + 0.25f) * 255.f) : c[i + k];
      int d = 0; for (int k = 0; k < 3; k++) d = std::max(d, std::abs((int)a[i + k] - exp[k])); if (d > 3) { if (!bad2) std::printf("first bad px %zu: gpu %d %d %d exp %d %d %d cov %d\n", i/4, a[i], a[i+1], a[i+2], exp[0], exp[1], exp[2], (int)cv); bad2++; }
    }
    std::printf("px(0,0) gpu %d %d %d  px(64,64) gpu %d %d %d ref %d %d %d\n", a[0], a[1], a[2], a[(64*W+64)*4], a[(64*W+64)*4+1], a[(64*W+64)*4+2], b[(64*W+64)*4], b[(64*W+64)*4+1], b[(64*W+64)*4+2]);
    std::printf("cbuffer variant %d: covered %d, mismatches %d\n", variant, cov2, bad2); CHECK(bad2 == 0 && cov2 > 800);
  }
  // pixel-shader cbuffer: o0 = sat(v1 * cb0[0] + cb0[1]), two different contents
  for (int variant = 0; variant < 2; variant++) {
    float cb[8] = {0.5f, 0.5f, 0.5f, 1,  0.1f, 0.2f, 0.3f, 0};
    if (variant) { cb[0] = 2; cb[1] = 0; cb[2] = 1; cb[4] = -0.4f; cb[5] = 0.5f; cb[6] = 0; }
    auto pcb = dxb::ps_cb_tint(); auto vsr = dxbc_to_spirv(vsb.data(), vsb.size()), psr = dxbc_to_spirv(pcb.data(), pcb.size()); CHECK(vsr.ok && psr.ok);
    d3d11::VulkanDrawBackend be(vk); be.SetShaders(vsr.words, psr.words);
    const float navy[4] = {0.02f, 0.03f, 0.2f, 1};
    std::vector<V> big = {{-1.6f,-1.6f,0,1, 1,0,0,1}, {1.6f,-1.6f,0,1, 0,1,0,1}, {0,1.6f,0,1, 0.2f,0.4f,1,1}};
    std::vector<V> small = big; for (auto& v : small) { v.x *= 0.5f; v.y *= 0.5f; }
    Rig gpu(&be), ref;
    CHECK(gpu.ctx.PSSetConstantBuffer(1, 1) == false);
    CHECK(gpu.ctx.PSSetConstantBuffer(0, gpu.dev.CreateBuffer(cb, sizeof cb)));
    gpu.ctx.ClearRenderTargetView(gpu.rtv, navy); ref.ctx.ClearRenderTargetView(ref.rtv, navy);
    CHECK(gpu.draw(big) && ref.draw(small));
    auto a = gpu.image(), b = ref.image(); Rig blank; blank.ctx.ClearRenderTargetView(blank.rtv, navy); auto c = blank.image();
    int badp = 0, covp = 0;
    for (size_t i = 0; i + 3 < a.size(); i += 4) {
      bool cv = b[i] != c[i] || b[i + 1] != c[i + 1] || b[i + 2] != c[i + 2]; if (cv) covp++;
      int ex[3]; for (int k = 0; k < 3; k++) ex[k] = cv ? (int)std::lround(sat(b[i + k] / 255.f * cb[k] + cb[4 + k]) * 255.f) : c[i + k];
      int d = 0; for (int k = 0; k < 3; k++) d = std::max(d, std::abs((int)a[i + k] - ex[k])); if (d > 4) badp++;
    }
    std::printf("pixel cbuffer variant %d: covered %d, mismatches %d\n", variant, covp, badp); CHECK(badp == 0 && covp > 1500);
  }
  // textures through the D3D11 model: full-target quad, translated shaders, CPU-evaluated expectation per pixel
  {
    struct VT { float x, y, z, w, r, g, b, a, u, v; };
    const std::vector<d3d11::InputElement> lay = {{"POSITION", d3d11::Format::R32G32B32A32_FLOAT, 0}, {"COLOR", d3d11::Format::R32G32B32A32_FLOAT, 16}, {"TEXCOORD", d3d11::Format::R32G32_FLOAT, 32}};
    auto run = [&](const std::vector<uint8_t>& psbytes, uint32_t tw, uint32_t th, const std::vector<uint8_t>& texels, d3d11::SamplerDesc sd, float uvs,
                   d3d11::Format tf, auto expect, int tol, const char* name) -> int {
      auto vsr = dxbc_to_spirv(dxb::vs_tex_pass().data(), dxb::vs_tex_pass().size()); auto psr = dxbc_to_spirv(psbytes.data(), psbytes.size());
      if (!vsr.ok || !psr.ok) { std::printf("translate failed: %s | %s\n", vsr.error.c_str(), psr.error.c_str()); return -1; }
      d3d11::VulkanDrawBackend be(vk); be.SetShaders(vsr.words, psr.words);
      d3d11::Device dev; d3d11::DeviceContext ctx(dev); ctx.SetDrawBackend(&be);
      auto rt = dev.CreateTexture2D(W, H, d3d11::Format::R8G8B8A8_UNORM); auto rtv = dev.CreateRenderTargetView(rt);
      auto tex = dev.CreateTexture2D(tw, th, tf); if (!dev.UpdateTexture(tex, texels.data(), texels.size())) return -1;
      ctx.IASetInputLayout(lay); ctx.IASetPrimitiveTopology(d3d11::Topology::TriangleList); ctx.RSSetViewport({0, 0, (float)W, (float)H}); ctx.OMSetRenderTarget(rtv);
      if (!ctx.PSSetShaderResource(0, tex) || !ctx.PSSetSampler(0, sd)) return -1;
      VT a{-1, 1, 0, 1, 1, 1, 1, 1, 0, 0}, b{1, 1, 0, 1, 1, 1, 1, 1, uvs, 0}, c{-1, -1, 0, 1, 1, 1, 1, 1, 0, uvs}, d{1, -1, 0, 1, 1, 1, 1, 1, uvs, uvs};
      std::vector<VT> q = {a, b, c, b, d, c};
      ctx.IASetVertexBuffer(dev.CreateBuffer(q.data(), q.size() * sizeof(VT)), sizeof(VT), 0);
      const float navy[4] = {0.02f, 0.03f, 0.2f, 1}; ctx.ClearRenderTargetView(rtv, navy);
      if (!ctx.Draw((uint32_t)q.size(), 0)) { std::printf("%s: draw failed: %s\n", name, ctx.last_error().c_str()); return -1; }
      std::vector<uint8_t> img; dev.ReadTexture(rt, &img);
      int bad = 0;
      for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int e[3]; expect(x, y, e);
        const uint8_t* p = &img[((size_t)y * W + x) * 4]; int dd = 0; for (int k = 0; k < 3; k++) dd = std::max(dd, std::abs((int)p[k] - e[k]));
        if (dd > tol) { if (!bad) std::printf("%s: first bad px (%d,%d) got %d %d %d exp %d %d %d\n", name, x, y, p[0], p[1], p[2], e[0], e[1], e[2]); bad++; }
      }
      std::printf("%s: mismatches %d\n", name, bad); return bad;
    };
    // 8x8 pattern texture
    std::vector<uint8_t> t8(8 * 8 * 4); for (int i = 0; i < 64; i++) { t8[i*4] = (uint8_t)((i * 37) & 255); t8[i*4+1] = (uint8_t)((i * 91 + 20) & 255); t8[i*4+2] = (uint8_t)((i * 53 + 100) & 255); t8[i*4+3] = 255; }
    auto near_exp = [&](int x, int y, int* e) { int tx = x / 16, ty = y / 16; for (int k = 0; k < 3; k++) e[k] = t8[(ty * 8 + tx) * 4 + k]; };
    CHECK(run(dxb::ps_sample(), 8, 8, t8, {false, false}, 1.0f, d3d11::Format::R8G8B8A8_UNORM, near_exp, 0, "nearest") == 0);
    CHECK(run(dxb::ps_sample(dxb::swz(0, 1, 2, 3), 0.0f), 8, 8, t8, {false, false}, 1.0f, d3d11::Format::R8G8B8A8_UNORM, near_exp, 0, "nearest sample_l") == 0);
    CHECK(run(dxb::ps_sample(dxb::swz(2, 1, 0, 3)), 8, 8, t8, {false, false}, 1.0f, d3d11::Format::R8G8B8A8_UNORM,
              [&](int x, int y, int* e) { int tx = x / 16, ty = y / 16; for (int k = 0; k < 3; k++) e[k] = t8[(ty * 8 + tx) * 4 + (2 - k)]; }, 0, "swizzle zyxw") == 0);
    // BGRA texture: the model's bytes are B,G,R,A, so the shader still sees the true colours
    { std::vector<uint8_t> bgra = t8; for (int i = 0; i < 64; i++) std::swap(bgra[i*4], bgra[i*4+2]);
      CHECK(run(dxb::ps_sample(), 8, 8, bgra, {false, false}, 1.0f, d3d11::Format::B8G8R8A8_UNORM, near_exp, 0, "bgra texture") == 0); }
    // wrap: uv 0..2 over a 4x4 texture
    { std::vector<uint8_t> t4(4 * 4 * 4); for (int i = 0; i < 16; i++) { t4[i*4] = (uint8_t)(i * 16); t4[i*4+1] = (uint8_t)(255 - i * 16); t4[i*4+2] = (uint8_t)(i * 7); t4[i*4+3] = 255; }
      CHECK(run(dxb::ps_sample(), 4, 4, t4, {false, true}, 2.0f, d3d11::Format::R8G8B8A8_UNORM,
                [&](int x, int y, int* e) { int tx = (x / 16) % 4, ty = (y / 16) % 4; for (int k = 0; k < 3; k++) e[k] = t4[(ty * 4 + tx) * 4 + k]; }, 0, "wrap") == 0); }
    // linear: 2x1 texture across u
    { std::vector<uint8_t> t2 = {250, 10, 0, 255,  0, 200, 100, 255};
      CHECK(run(dxb::ps_sample(), 2, 1, t2, {true, false}, 1.0f, d3d11::Format::R8G8B8A8_UNORM,
                [&](int x, int, int* e) { float u = (x + 0.5f) / W, f = (u - 0.25f) / 0.5f; f = f < 0 ? 0 : f > 1 ? 1 : f;
                  for (int k = 0; k < 3; k++) e[k] = (int)std::lround(t2[k] * (1 - f) + t2[4 + k] * f); }, 4, "linear") == 0); }
    // a texture bound with the pass-through shaders is an error, not a silent skip
    { d3d11::VulkanDrawBackend be(vk); d3d11::Device dev; d3d11::DeviceContext ctx(dev); ctx.SetDrawBackend(&be);
      auto rt = dev.CreateTexture2D(W, H, d3d11::Format::R8G8B8A8_UNORM); auto rtv = dev.CreateRenderTargetView(rt);
      auto tex = dev.CreateTexture2D(2, 2, d3d11::Format::R8G8B8A8_UNORM);
      ctx.IASetInputLayout(lay); ctx.IASetPrimitiveTopology(d3d11::Topology::TriangleList); ctx.RSSetViewport({0, 0, (float)W, (float)H}); ctx.OMSetRenderTarget(rtv);
      CHECK(ctx.PSSetShaderResource(0, tex));
      VT t[3] = {{-.5f,-.5f,0,1, 1,1,1,1, 0,0}, {.5f,-.5f,0,1, 1,1,1,1, 1,0}, {0,.5f,0,1, 1,1,1,1, 0,1}};
      ctx.IASetVertexBuffer(dev.CreateBuffer(t, sizeof t), sizeof(VT), 0);
      CHECK(!ctx.Draw(3, 0) && ctx.last_error().find("no shaders") != std::string::npos);
      std::printf("texture without shaders rejected\n"); }
  }
  // control flow through the D3D11 model + Vulkan: full-target quad, vertex colour = (x/W, y/H, 0), per-pixel CPU expectation
  {
    struct VC { float x, y, z, w, r, g, b, a; };
    auto quad_run = [&](const std::vector<uint8_t>& psbytes, auto expect, int tol, const char* name) -> int {
      auto vsb2 = dxb::vs_color_pass(); auto vsr = dxbc_to_spirv(vsb2.data(), vsb2.size()); auto psr = dxbc_to_spirv(psbytes.data(), psbytes.size());
      if (!vsr.ok || !psr.ok) { std::printf("%s: translate failed: %s | %s\n", name, vsr.error.c_str(), psr.error.c_str()); return -1; }
      d3d11::VulkanDrawBackend be(vk); be.SetShaders(vsr.words, psr.words);
      Rig gpu(&be);
      VC a{-1, 1, 0, 1, 0, 0, 0, 1}, b{1, 1, 0, 1, 1, 0, 0, 1}, c{-1, -1, 0, 1, 0, 1, 0, 1}, d{1, -1, 0, 1, 1, 1, 0, 1};
      std::vector<VC> q = {a, b, c, b, d, c};
      gpu.ctx.IASetVertexBuffer(gpu.dev.CreateBuffer(q.data(), q.size() * sizeof(VC)), sizeof(VC), 0);
      const float navy[4] = {0.02f, 0.03f, 0.2f, 1}; gpu.ctx.ClearRenderTargetView(gpu.rtv, navy);
      if (!gpu.ctx.Draw((uint32_t)q.size(), 0)) { std::printf("%s: draw failed: %s\n", name, gpu.ctx.last_error().c_str()); return -1; }
      auto img = gpu.image(); int bad = 0;
      for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int e[3]; expect(x, y, e);
        const uint8_t* p = &img[((size_t)y * W + x) * 4]; int dd = 0; for (int k = 0; k < 3; k++) dd = std::max(dd, std::abs((int)p[k] - e[k]));
        if (dd > tol) { if (!bad) std::printf("%s: first bad px (%d,%d) got %d %d %d exp %d %d %d\n", name, x, y, p[0], p[1], p[2], e[0], e[1], e[2]); bad++; }
      }
      std::printf("%s: mismatches %d\n", name, bad); return bad;
    };
    auto ux = [](int x) { return (x + 0.5f) / W; };
    CHECK(quad_run(dxb::ps_if_else(true), [&](int x, int, int* e) { bool l = ux(x) < 0.5f; e[0] = l ? 255 : 0; e[1] = l ? 0 : 255; e[2] = 0; }, 0, "if/else") == 0);
    CHECK(quad_run(dxb::ps_if_else(false), [&](int x, int, int* e) { bool l = ux(x) < 0.5f; e[0] = l ? 255 : 0; e[1] = 0; e[2] = l ? 0 : 255; }, 0, "if without else") == 0);
    CHECK(quad_run(dxb::ps_loop(false), [&](int x, int, int* e) { for (int k = 0; k < 3; k++) e[k] = (int)std::lround(ux(x) * 255.f); }, 2, "loop + breakc") == 0);
    CHECK(quad_run(dxb::ps_loop(true), [&](int x, int, int* e) { for (int k = 0; k < 3; k++) e[k] = (int)std::lround(ux(x) * 255.f); }, 2, "loop + continuec") == 0);
    CHECK(quad_run(dxb::ps_discard(), [&](int x, int, int* e) { bool l = ux(x) < 0.5f; e[0] = l ? 5 : 0; e[1] = l ? 8 : 255; e[2] = l ? 51 : 0; }, 1, "discard") == 0);
    CHECK(quad_run(dxb::ps_int_movc(), [&](int x, int y, int* e) {
              float uy = (y + 0.5f) / H; bool red = uy < 0.5f;     // v1.y is 0 at the top, 1 at the bottom
              if (red) { e[0] = 255; e[1] = 0; e[2] = 0; } else { float q = std::floor(ux(x) * 4.f) / 4.f; for (int k = 0; k < 3; k++) e[k] = (int)std::lround(q * 255.f); } }, 2, "ftoi/itof/movc") == 0);
    // vertex early return: the y flip must still be applied; compare with the reference renderer on the CPU-offset triangle
    { auto vsb2 = dxb::vs_early_ret(), psb2 = dxb::ps_pass(); auto vsr = dxbc_to_spirv(vsb2.data(), vsb2.size()), psr = dxbc_to_spirv(psb2.data(), psb2.size());
      CHECK(vsr.ok && psr.ok);
      d3d11::VulkanDrawBackend be(vk); be.SetShaders(vsr.words, psr.words);
      const float navy[4] = {0.02f, 0.03f, 0.2f, 1};
      std::vector<V> t = {{-0.6f,-0.7f,0,1, 1,0,0,1}, {0.6f,-0.7f,0,1, 0,1,0,1}, {0,0.5f,0,1, 0,0,1,1}};
      std::vector<V> moved = t; for (auto& v : moved) v.y += 0.25f;
      Rig gpu(&be), ref; gpu.ctx.ClearRenderTargetView(gpu.rtv, navy); ref.ctx.ClearRenderTargetView(ref.rtv, navy);
      CHECK(gpu.draw(t) && ref.draw(moved));
      auto a = gpu.image(), b = ref.image(); int bad = 0, cov = 0;
      for (size_t i = 0; i + 3 < a.size(); i += 4) { int d = 0; for (int k = 0; k < 3; k++) d = std::max(d, std::abs((int)a[i + k] - (int)b[i + k])); if (d > 3) bad++; if (b[i] > 40) cov++; }
      std::printf("vertex early ret (y flip kept): covered %d, mismatches %d\n", cov, bad); CHECK(bad == 0 && cov > 800); }
  }
  // an oversized constant buffer is an error, not a truncation
  { auto vcb = dxb::vs_cb_scale_offset(); auto vsr = dxbc_to_spirv(vcb.data(), vcb.size()), psr = dxbc_to_spirv(psb.data(), psb.size());
    d3d11::VulkanDrawBackend be(vk); be.SetShaders(vsr.words, psr.words);
    Rig gpu(&be); std::vector<uint8_t> big(256, 0); CHECK(gpu.ctx.VSSetConstantBuffer(0, gpu.dev.CreateBuffer(big.data(), big.size())));
    std::vector<V> t = {{-0.5f,-0.5f,0,1, 1,0,0,1}, {0.5f,-0.5f,0,1, 0,1,0,1}, {0,0.5f,0,1, 0,0,1,1}};
    CHECK(!gpu.draw(t) && gpu.ctx.last_error().find("128") != std::string::npos);
    std::printf("oversized cbuffer rejected\n"); }
  std::printf("dxbc: all ok\n");
  return 0;
}
