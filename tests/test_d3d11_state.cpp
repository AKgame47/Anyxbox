// Depth test/write, blending and culling in the D3D11 model: exact checks on the reference renderer, then the same
// scenes through the Vulkan backend (software GPU) compared with it. Skips the GPU half when Vulkan is missing.
// Colours come from a pixel-shader cbuffer on the Vulkan path and from the vertex colour on the reference path;
// the test feeds both the same value, because the Vulkan path does not carry vertex alpha.
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
#include "dxbc_builder.h"
#include "gfx/d3d11_vulkan.h"
#include "shader/dxbc_spirv.h"
using namespace anyxbox;
using namespace anyxbox::d3d11;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
static const int W = 128, H = 128;
struct V { float x, y, z, w, r, g, b, a; };
struct Col { float r, g, b, a; };

static std::vector<uint8_t> vs_pass() {
  using namespace dxb;
  return dxb::shader(1, {{"POSITION", 0, 0, 0, 0xF}, {"COLOR", 0, 0, 1, 0x7}}, {{"SV_Position", 0, 1, 0, 0xF}, {"COLOR", 0, 0, 1, 0xF}},
    {ins(95, {dst(T_INPUT, 0, 0xF)}), ins(95, {dst(T_INPUT, 1, 0x7)}), ins(103, {dst(T_OUTPUT, 0, 0xF)}, false, 0, {1}), ins(101, {dst(T_OUTPUT, 1, 0xF)}),
     ins(OP_MOV, {dst(T_OUTPUT, 0, 0xF), src(T_INPUT, 0)}), ins(OP_MOV, {dst(T_OUTPUT, 1, 0xF), src(T_INPUT, 1, swz(0, 1, 2, 0))}), ins(OP_RET, {})});
}
static std::vector<uint8_t> ps_cb_color() {   // o0 = cb0[0]
  using namespace dxb;
  return dxb::shader(0, {{"COLOR", 0, 0, 1, 0xF}}, {{"SV_Target", 0, 0, 0, 0xF}},
    {dcl_cb(0, 1), ins(98, {dst(T_INPUT, 1, 0xF)}, false, 2), ins(101, {dst(T_OUTPUT, 0, 0xF)}),
     ins(OP_MOV, {dst(T_OUTPUT, 0, 0xF), src_cb(0, 0)}), ins(OP_RET, {})});
}

struct Rig {
  Device dev; DeviceContext ctx; ResourceId tex = 0, rtv = 0, db = 0, dsv = 0;
  std::unique_ptr<VulkanDrawBackend> be;
  Rig(pnr::graphics::VulkanBackend* vk, const std::vector<uint32_t>& vs, const std::vector<uint32_t>& ps) : ctx(dev) {
    if (vk) { be.reset(new VulkanDrawBackend(*vk)); be->SetShaders(vs, ps); ctx.SetDrawBackend(be.get()); }
    tex = dev.CreateTexture2D(W, H, Format::R8G8B8A8_UNORM); rtv = dev.CreateRenderTargetView(tex);
    db = dev.CreateDepthBuffer(W, H); dsv = dev.CreateDepthStencilView(db);
    ctx.IASetInputLayout({{"POSITION", Format::R32G32B32A32_FLOAT, 0}, {"COLOR", Format::R32G32B32A32_FLOAT, 16}});
    ctx.IASetPrimitiveTopology(Topology::TriangleList); ctx.RSSetViewport({0, 0, (float)W, (float)H});
    ctx.OMSetRenderTarget(rtv, dsv);
    const float c[4] = {0, 0, 0, 1}; ctx.ClearRenderTargetView(rtv, c); ctx.ClearDepthStencilView(dsv, 1.0f);
    RasterizerDesc r; r.cull = Cull::None; ctx.RSSetState(r);
  }
  bool tri(float ax, float ay, float bx, float by, float cx, float cy, float z, Col c) {
    V v[3] = {{ax, ay, z, 1, c.r, c.g, c.b, c.a}, {bx, by, z, 1, c.r, c.g, c.b, c.a}, {cx, cy, z, 1, c.r, c.g, c.b, c.a}};
    ctx.IASetVertexBuffer(dev.CreateBuffer(v, sizeof v), sizeof(V), 0);
    float cb[4] = {c.r, c.g, c.b, c.a}; ctx.PSSetConstantBuffer(0, dev.CreateBuffer(cb, 16));
    return ctx.Draw(3, 0);
  }
  std::vector<uint8_t> image() { std::vector<uint8_t> o; dev.ReadTexture(tex, &o); return o; }
};
static const uint8_t* px(const std::vector<uint8_t>& im, int x, int y) { return &im[((size_t)y * W + x) * 4]; }
static bool near_c(const uint8_t* p, int r, int g, int b, int tol = 3) { return std::abs(p[0] - r) <= tol && std::abs(p[1] - g) <= tol && std::abs(p[2] - b) <= tol; }
static int mismatches(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int tol = 3) {
  int n = 0;
  for (size_t i = 0; i + 3 < a.size(); i += 4) { int d = 0; for (int k = 0; k < 3; k++) d = std::max(d, std::abs((int)a[i + k] - (int)b[i + k])); if (d > tol) n++; }
  return n;
}
// sample points (see the geometry notes in the scenes)
static const int OX = 83, OY = 64;   // overlap of A and B
static const int AX = 45, AY = 96;   // A only
static const int BX = 105, BY = 76;  // B only
static const Col RED = {1, 0, 0, 1}, GREEN = {0, 1, 0, 1}, BLUE = {0, 0, 1, 1};

static void sceneA(Rig& r, float za, float zb, Col ca, Col cb) {   // A: big, B: smaller, partly overlapping
  r.tri(-0.9f, -0.9f, 0.9f, -0.9f, 0, 0.9f, za, ca);
  r.tri(-0.3f, -0.3f, 0.9f, -0.3f, 0.3f, 0.9f, zb, cb);
}
static void sceneB(Rig& r, float za, float zb, Col ca, Col cb) {   // B first
  r.tri(-0.3f, -0.3f, 0.9f, -0.3f, 0.3f, 0.9f, zb, cb);
  r.tri(-0.9f, -0.9f, 0.9f, -0.9f, 0, 0.9f, za, ca);
}
typedef std::function<void(Rig&)> Scene;
struct Case { const char* name; Scene run; std::function<bool(const std::vector<uint8_t>&)> check; };

int main() {
  std::vector<Case> cases;
  auto depth_state = [](Rig& r, bool en, bool wr, Compare f) { DepthStencilDesc d; d.depth_enable = en; d.depth_write = wr; d.func = f; r.ctx.OMSetDepthStencilState(d); };
  auto blend_state = [](Rig& r, BlendFactor s, BlendFactor d, BlendOp op) { BlendDesc b; b.enable = true; b.src = s; b.dst = d; b.op = op; b.src_a = BlendFactor::One; b.dst_a = BlendFactor::Zero; r.ctx.OMSetBlendState(b); };
  auto no_depth = [&](Rig& r) { depth_state(r, false, false, Compare::Always); };

  // ---- depth ----
  cases.push_back({"depth less, far then near", [&](Rig& r) { sceneA(r, 0.8f, 0.2f, RED, GREEN); },
    [](const std::vector<uint8_t>& im) { return near_c(px(im, OX, OY), 0, 255, 0) && near_c(px(im, AX, AY), 255, 0, 0) && near_c(px(im, BX, BY), 0, 255, 0); }});
  cases.push_back({"depth less, near then far (far loses)", [&](Rig& r) { sceneB(r, 0.8f, 0.2f, RED, GREEN); },
    [](const std::vector<uint8_t>& im) { return near_c(px(im, OX, OY), 0, 255, 0) && near_c(px(im, AX, AY), 255, 0, 0) && near_c(px(im, BX, BY), 0, 255, 0); }});
  cases.push_back({"depth disabled, last draw wins", [&](Rig& r) { no_depth(r); sceneB(r, 0.8f, 0.2f, RED, GREEN); },
    [](const std::vector<uint8_t>& im) { return near_c(px(im, OX, OY), 255, 0, 0); }});
  cases.push_back({"depth greater vs cleared 1.0: nothing passes", [&](Rig& r) { depth_state(r, true, true, Compare::Greater); sceneA(r, 0.8f, 0.2f, RED, GREEN); },
    [](const std::vector<uint8_t>& im) { return near_c(px(im, OX, OY), 0, 0, 0) && near_c(px(im, AX, AY), 0, 0, 0); }});
  cases.push_back({"depth always, write on: later draw wins, then less test sees its depth", [&](Rig& r) {
      depth_state(r, true, true, Compare::Always); sceneB(r, 0.8f, 0.2f, RED, GREEN);   // A (z .8) last: overlap red, depth .8
      depth_state(r, true, true, Compare::Less); r.tri(-0.3f, -0.3f, 0.9f, -0.3f, 0.3f, 0.9f, 0.5f, BLUE); },   // z .5 < .8 in overlap and B-only(.2 stored, .5 fails)
    [](const std::vector<uint8_t>& im) { return near_c(px(im, OX, OY), 0, 0, 255) && near_c(px(im, BX, BY), 0, 255, 0); }});
  cases.push_back({"depth write off: later draw still tests against cleared depth", [&](Rig& r) {
      depth_state(r, true, false, Compare::Less); sceneA(r, 0.2f, 0.8f, RED, GREEN); },   // A near, B far; nothing written, so B draws over A
    [](const std::vector<uint8_t>& im) { return near_c(px(im, OX, OY), 0, 255, 0); }});
  // ---- blending (depth off) ----
  Scene alpha = [&](Rig& r) { no_depth(r); r.tri(-0.9f, -0.9f, 0.9f, -0.9f, 0, 0.9f, 0.5f, BLUE);
      blend_state(r, BlendFactor::SrcAlpha, BlendFactor::InvSrcAlpha, BlendOp::Add);
      r.tri(-0.3f, -0.3f, 0.9f, -0.3f, 0.3f, 0.9f, 0.5f, {1, 0, 0, 0.5f}); };
  cases.push_back({"alpha blend 50% red over blue / black", alpha,
    [](const std::vector<uint8_t>& im) { return near_c(px(im, OX, OY), 128, 0, 128) && near_c(px(im, BX, BY), 128, 0, 0) && near_c(px(im, AX, AY), 0, 0, 255); }});
  auto two = [&](BlendFactor s, BlendFactor d, BlendOp op, Col top) { return Scene([=](Rig& r) { no_depth(r);
      r.tri(-0.9f, -0.9f, 0.9f, -0.9f, 0, 0.9f, 0.5f, BLUE);
      BlendDesc b; b.enable = true; b.src = s; b.dst = d; b.op = op; b.src_a = BlendFactor::One; b.dst_a = BlendFactor::Zero; r.ctx.OMSetBlendState(b);
      r.tri(-0.3f, -0.3f, 0.9f, -0.3f, 0.3f, 0.9f, 0.5f, top); }); };
  cases.push_back({"additive", two(BlendFactor::One, BlendFactor::One, BlendOp::Add, RED),
    [](const std::vector<uint8_t>& im) { return near_c(px(im, OX, OY), 255, 0, 255) && near_c(px(im, BX, BY), 255, 0, 0); }});
  cases.push_back({"min", two(BlendFactor::One, BlendFactor::One, BlendOp::Min, RED),
    [](const std::vector<uint8_t>& im) { return near_c(px(im, OX, OY), 0, 0, 0); }});
  cases.push_back({"subtract (src - dst)", two(BlendFactor::One, BlendFactor::One, BlendOp::Subtract, RED),
    [](const std::vector<uint8_t>& im) { return near_c(px(im, OX, OY), 255, 0, 0); }});
  cases.push_back({"reverse subtract (dst - src)", two(BlendFactor::One, BlendFactor::One, BlendOp::RevSubtract, RED),
    [](const std::vector<uint8_t>& im) { return near_c(px(im, OX, OY), 0, 0, 255); }});
  cases.push_back({"dest colour factor (multiply)", two(BlendFactor::DestColor, BlendFactor::Zero, BlendOp::Add, {1, 1, 0.5f, 1}),
    [](const std::vector<uint8_t>& im) { return near_c(px(im, OX, OY), 0, 0, 128) && near_c(px(im, BX, BY), 0, 0, 0); }});
  // ---- culling: visually clockwise triangle (left-bottom, top, right-bottom in y-up clip space) ----
  auto cull2 = [&](Cull c, bool ccw) { return Scene([=](Rig& r) { RasterizerDesc rd; rd.cull = c; rd.front_ccw = ccw; r.ctx.RSSetState(rd); no_depth(r);
      r.tri(-0.8f, -0.8f, 0, 0.0f, 0.8f, -0.8f, 0.5f, RED);          // CW on screen, lower part
      r.tri(-0.8f, 0.9f, 0, 0.1f, 0.8f, 0.9f, 0.5f, GREEN); }); };   // CCW on screen (left, bottom apex, right), upper part
  // sample: red region NDC (0,-0.5) -> x 64, y 96 ; green region NDC (0, 0.7) -> x 64, y 19
  cases.push_back({"cull back (default front = clockwise): CW drawn, CCW culled", cull2(Cull::Back, false),
    [](const std::vector<uint8_t>& im) { return near_c(px(im, 64, 96), 255, 0, 0) && near_c(px(im, 64, 19), 0, 0, 0); }});
  cases.push_back({"cull front: CCW drawn, CW culled", cull2(Cull::Front, false),
    [](const std::vector<uint8_t>& im) { return near_c(px(im, 64, 96), 0, 0, 0) && near_c(px(im, 64, 19), 0, 255, 0); }});
  cases.push_back({"cull back with FrontCounterClockwise: CCW drawn", cull2(Cull::Back, true),
    [](const std::vector<uint8_t>& im) { return near_c(px(im, 64, 96), 0, 0, 0) && near_c(px(im, 64, 19), 0, 255, 0); }});
  cases.push_back({"cull none draws both", cull2(Cull::None, false),
    [](const std::vector<uint8_t>& im) { return near_c(px(im, 64, 96), 255, 0, 0) && near_c(px(im, 64, 19), 0, 255, 0); }});

  auto scissor_scene = [&](ScissorRect rect, bool enabled) { return Scene([=](Rig& r) {
    no_depth(r); RasterizerDesc rs; rs.cull = Cull::None; rs.scissor_enable = enabled;
    r.ctx.RSSetState(rs); r.ctx.RSSetScissorRect(rect);
    r.tri(-1,-1,1,-1,-1,1,0.5f,RED); r.tri(-1,1,1,-1,1,1,0.5f,RED);
  }); };
  cases.push_back({"scissor half-open edges",scissor_scene({20,30,80,90},true),[](const std::vector<uint8_t>& im) {
    return near_c(px(im,20,30),255,0,0) && near_c(px(im,79,89),255,0,0) && near_c(px(im,19,30),0,0,0) && near_c(px(im,80,89),0,0,0) && near_c(px(im,79,90),0,0,0); }});
  cases.push_back({"scissor empty discards",scissor_scene({},true),[](const std::vector<uint8_t>& im) { return near_c(px(im,64,64),0,0,0); }});
  cases.push_back({"scissor off ignores rectangle",scissor_scene({},false),[](const std::vector<uint8_t>& im) { return near_c(px(im,64,64),255,0,0); }});
  cases.push_back({"scissor clips negative bounds",scissor_scene({-50,-20,40,40},true),[](const std::vector<uint8_t>& im) { return near_c(px(im,0,0),255,0,0) && near_c(px(im,39,39),255,0,0) && near_c(px(im,40,40),0,0,0); }});
  cases.push_back({"scissor outside target",scissor_scene({200,200,300,300},true),[](const std::vector<uint8_t>& im) { return near_c(px(im,64,64),0,0,0); }});

  cases.push_back({"scissor moves and preserves prior draw",[&](Rig& r) {
    no_depth(r); RasterizerDesc rs; rs.cull = Cull::None; rs.scissor_enable = true; r.ctx.RSSetState(rs);
    auto quad = [&](Col c) { r.tri(-1,-1,1,-1,-1,1,0.5f,c); r.tri(-1,1,1,-1,1,1,0.5f,c); };
    r.ctx.RSSetScissorRect({0,0,64,128}); quad(RED);
    r.ctx.RSSetScissorRect({64,0,128,128}); quad(GREEN);
  },[](const std::vector<uint8_t>& im) { return near_c(px(im,30,40),255,0,0) && near_c(px(im,90,40),0,255,0); }});
  cases.push_back({"viewport offset and reduced size",[&](Rig& r) {
    no_depth(r); r.ctx.RSSetViewport({20,30,60,50});
    r.tri(-1,-1,1,-1,-1,1,0.5f,RED); r.tri(-1,1,1,-1,1,1,0.5f,RED);
  },[](const std::vector<uint8_t>& im) { return near_c(px(im,25,40),255,0,0) && near_c(px(im,75,75),255,0,0) && near_c(px(im,19,40),0,0,0) && near_c(px(im,80,70),0,0,0); }});
  cases.push_back({"viewport depth range transforms stored depth",[&](Rig& r) {
    r.ctx.RSSetViewport({0,0,128,128,0.7f,0.9f}); r.tri(-1,-1,1,-1,-1,1,0.5f,RED);
    r.ctx.RSSetViewport({0,0,128,128,0,1}); r.tri(-1,-1,1,-1,-1,1,0.6f,GREEN);
  },[](const std::vector<uint8_t>& im) { return near_c(px(im,30,90),0,255,0); }});
  cases.push_back({"host color replacement preserves depth",[&](Rig& r) {
    r.tri(-1,-1,1,-1,-1,1,0.2f,RED);
    std::vector<uint8_t> blue(W*H*4);for(size_t k=0;k<blue.size();k+=4){blue[k+2]=255;blue[k+3]=255;}
    r.dev.UpdateTexture(r.tex,blue.data(),blue.size());
    r.tri(-1,-1,1,-1,-1,1,0.8f,GREEN);
  },[](const std::vector<uint8_t>& im){return near_c(px(im,30,90),0,0,255);}});
  // reference pass: exact expectations
  std::vector<std::vector<uint8_t>> ref;
  for (auto& c : cases) {
    Rig r(nullptr, {}, {}); c.run(r);
    if (!r.ctx.last_error().empty()) { std::printf("FAIL %s: %s\n", c.name, r.ctx.last_error().c_str()); return 1; }
    ref.push_back(r.image());
    if (std::getenv("ANYXBOX_SCISSOR_PREVIEW") && (std::string(c.name) == "scissor half-open edges" || std::string(c.name) == "viewport offset and reduced size")) {
      FILE* f = std::fopen("/downloads/scissor-reference.ppm", "wb");
      if (f) { std::fprintf(f,"P6\n%d %d\n255\n",W,H); for (size_t i=0;i<ref.back().size();i+=4) std::fwrite(ref.back().data()+i,1,3,f); std::fclose(f); }
    }
    if (!c.check(ref.back())) { std::printf("FAIL reference: %s\n", c.name); return 1; }
    std::printf("reference ok: %s\n", c.name);
  }
  // a depth buffer of the wrong size is rejected, not read past
  { Rig r(nullptr, {}, {}); auto small = r.dev.CreateDepthStencilView(r.dev.CreateDepthBuffer(64, 64)); r.ctx.OMSetRenderTarget(r.rtv, small);
    CHECK(!r.tri(-0.5f, -0.5f, 0.5f, -0.5f, 0, 0.5f, 0.5f, RED)); }

  // Vulkan pass
  pnr::graphics::VulkanBackend vk;
  auto s = vk.open();
  if (!s) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  s = vk.create_instance("anyxbox-test");
  if (!s || vk.devices().empty()) { std::printf("SKIP gpu part: no device\n"); return 0; }
  if (!(s = vk.create_device(0))) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  auto vsb = vs_pass(), psb = ps_cb_color();
  auto vs = shader::dxbc_to_spirv(vsb.data(), vsb.size()), ps = shader::dxbc_to_spirv(psb.data(), psb.size());
  if (!vs.ok || !ps.ok) { std::printf("translate failed: %s | %s\n", vs.error.c_str(), ps.error.c_str()); return 1; }
  for (size_t i = 0; i < cases.size(); i++) {
    Rig r(&vk, vs.words, ps.words); cases[i].run(r);
    if (!r.ctx.last_error().empty()) { std::printf("FAIL vulkan %s: %s\n", cases[i].name, r.ctx.last_error().c_str()); return 1; }
    auto img = r.image();
    if (std::getenv("ANYXBOX_SCISSOR_PREVIEW") && (std::string(cases[i].name) == "scissor half-open edges" || std::string(cases[i].name) == "viewport offset and reduced size")) {
      FILE* f = std::fopen("/downloads/scissor-vulkan.ppm","wb"); if (f) { std::fprintf(f,"P6\n%d %d\n255\n",W,H); for(size_t k=0;k<img.size();k+=4) std::fwrite(img.data()+k,1,3,f); std::fclose(f); }
    }
    int bad = mismatches(img, ref[i]);
    std::printf("vulkan vs reference: %-70s mismatches %d\n", cases[i].name, bad);
    CHECK(bad == 0);
  }
  std::printf("d3d11 state: all ok\n");
  return 0;
}
