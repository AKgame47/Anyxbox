#include <cmath>
#include <cstdio>
#include <cstring>
#include "gfx/d3d11_model.h"
using namespace anyxbox::d3d11;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
struct Vtx { float x, y, z, r, g, b, a; };
static bool near(int a, int b, int tol = 1) { return std::abs(a - b) <= tol; }
static const uint8_t* px(const std::vector<uint8_t>& im, int w, int x, int y) { return &im[((size_t)y * w + x) * 4]; }

int main() {
  // Automatic mip reduction: all four channels, odd dimensions, regeneration and invalid IDs.
  for (auto f : {Format::R8G8B8A8_UNORM,Format::B8G8R8A8_UNORM}) {
    Device d; auto t = d.CreateTexture2D(3,1,f,0); CHECK(t);
    const uint8_t raw[12]={0,30,60,90,30,60,90,120,60,90,120,150};
    CHECK(d.UpdateTexture(t,raw,sizeof raw) && d.GenerateMips(t));
    CHECK(d.texture(t)->mips.size()==1 && d.texture(t)->mips[0]==std::vector<uint8_t>({30,60,90,120}));
    std::vector<uint8_t> solid(12,200); CHECK(d.UpdateTexture(t,solid.data(),solid.size()) && d.GenerateMips(t));
    CHECK(d.texture(t)->mips[0]==std::vector<uint8_t>(4,200)); CHECK(!d.GenerateMips(0));
    auto square=d.CreateTexture2D(4,4,f,0); std::vector<uint8_t> pattern(64,0);
    for(size_t i=0;i<16;++i) { pattern[i*4]=(uint8_t)(i*4); pattern[i*4+3]=255; }
    CHECK(d.UpdateTexture(square,pattern.data(),pattern.size()) && d.GenerateMips(square));
    CHECK(d.texture(square)->mips[0][0]==10 && d.texture(square)->mips[1][0]==30 && d.texture(square)->mips[1][3]==255);
  }
  const int W = 64, H = 64;
  std::vector<InputElement> layout = {{"POSITION", Format::R32G32B32_FLOAT, 0}, {"COLOR", Format::R32G32B32A32_FLOAT, 12}};
  Device dev; DeviceContext ctx(dev);
  ResourceId tex = dev.CreateTexture2D(W, H, Format::R8G8B8A8_UNORM);
  ResourceId rtv = dev.CreateRenderTargetView(tex);
  CHECK(tex && rtv);
  auto setup = [&](const std::vector<Vtx>& v) {
    ResourceId vb = dev.CreateBuffer(v.data(), v.size() * sizeof(Vtx));
    ctx.IASetInputLayout(layout); ctx.IASetVertexBuffer(vb, sizeof(Vtx), 0);
    ctx.IASetPrimitiveTopology(Topology::TriangleList);
    ctx.RSSetViewport({0, 0, (float)W, (float)H}); ctx.OMSetRenderTarget(rtv);
    return vb;
  };
  const float black[4] = {0, 0, 0, 1};
  std::vector<uint8_t> im;

  // 1. Colour triangle: red at lower left, green at lower right, blue at upper left.
  ctx.ClearRenderTargetView(rtv, black);
  setup({{-1, -1, 0, 1, 0, 0, 1}, {1, -1, 0, 0, 1, 0, 1}, {-1, 1, 0, 0, 0, 1, 1}});
  CHECK(ctx.Draw(3, 0));
  CHECK(dev.ReadTexture(tex, &im));
  // pixel (10,50): u = 10.5/64, v = 13.5/64, red weight = 1-u-v (computed by hand)
  { auto p = px(im, W, 10, 50);
    CHECK(near(p[0], 159) && near(p[1], 42) && near(p[2], 54) && p[3] == 255); }
  CHECK(px(im, W, 63, 0)[0] == 0 && px(im, W, 63, 0)[1] == 0 && px(im, W, 63, 0)[2] == 0);   // outside: clear colour
  CHECK(px(im, W, 1, 62)[0] > 240);                                                       // near the red corner
  CHECK(px(im, W, 62, 63)[1] > 235 && px(im, W, 62, 63)[0] < 15);                         // near the green corner
  CHECK(px(im, W, 0, 1)[2] > 235);                                                        // near the blue corner
  // coverage: the half-plane below the diagonal, diagonal included or excluded (top-left rule), never partial
  { uint64_t n = ctx.stats().pixels_written; CHECK(n == 2016 || n == 2080); }

  // 2. Full-screen quad as two triangles: every pixel written exactly once (shared edge, no seam, no overlap)
  ctx.ClearRenderTargetView(rtv, black);
  DeviceContext q(dev);
  { std::vector<Vtx> v = {{-1,-1,0,1,1,1,1},{-1,1,0,1,1,1,1},{1,1,0,1,1,1,1}, {-1,-1,0,1,1,1,1},{1,1,0,1,1,1,1},{1,-1,0,1,1,1,1}};
    ResourceId vb = dev.CreateBuffer(v.data(), v.size() * sizeof(Vtx));
    q.IASetInputLayout(layout); q.IASetVertexBuffer(vb, sizeof(Vtx), 0); q.IASetPrimitiveTopology(Topology::TriangleList);
    q.RSSetViewport({0, 0, (float)W, (float)H}); q.OMSetRenderTarget(rtv);
    CHECK(q.Draw(6, 0));
    CHECK(q.stats().pixels_written == (uint64_t)W * H);
    dev.ReadTexture(tex, &im);
    int white = 0; for (int i = 0; i < W * H; i++) if (im[i*4] == 255 && im[i*4+1] == 255 && im[i*4+2] == 255) white++;
    CHECK(white == W * H); }

  // 3. Winding does not matter (no culling in this slice): same triangle, reversed order, same pixel count
  { Device d2; DeviceContext c2(d2);
    auto t2 = d2.CreateTexture2D(W, H, Format::R8G8B8A8_UNORM); auto r2 = d2.CreateRenderTargetView(t2);
    std::vector<Vtx> v = {{-1,-1,0,1,0,0,1},{-1,1,0,0,0,1,1},{1,-1,0,0,1,0,1}};
    auto vb = d2.CreateBuffer(v.data(), v.size() * sizeof(Vtx));
    c2.IASetInputLayout(layout); c2.IASetVertexBuffer(vb, sizeof(Vtx), 0); c2.IASetPrimitiveTopology(Topology::TriangleList);
    c2.RSSetViewport({0, 0, (float)W, (float)H}); c2.OMSetRenderTarget(r2);
    CHECK(c2.Draw(3, 0)); CHECK(c2.stats().pixels_written == ctx.stats().pixels_written);
    std::vector<uint8_t> im2; d2.ReadTexture(t2, &im2);
    CHECK(near(px(im2, W, 10, 50)[0], 159)); }

  // 4. Format: BGRA target swaps red and blue bytes
  { Device d3; DeviceContext c3(d3);
    auto t3 = d3.CreateTexture2D(4, 4, Format::B8G8R8A8_UNORM); auto r3 = d3.CreateRenderTargetView(t3);
    const float red[4] = {1, 0, 0, 1}; c3.ClearRenderTargetView(r3, red);
    std::vector<uint8_t> o; d3.ReadTexture(t3, &o);
    CHECK(o[0] == 0 && o[1] == 0 && o[2] == 255 && o[3] == 255); }

  // 5. Viewport offset and perspective-correct colour: triangle with w=2 on one vertex
  { Device d4; DeviceContext c4(d4);
    auto t4 = d4.CreateTexture2D(W, H, Format::R8G8B8A8_UNORM); auto r4 = d4.CreateRenderTargetView(t4);
    // clip positions: (x/w, y/w) are the same as case 1 except vertex 1 has w=2 (x=2 -> ndc 1)
    struct V4 { float x, y, z, w, r, g, b, a; };
    std::vector<V4> v = {{-1,-1,0,1,1,0,0,1},{2,-2,0,2,0,1,0,1},{-1,1,0,1,0,0,1,1}};
    auto vb = d4.CreateBuffer(v.data(), v.size() * sizeof(V4));
    c4.IASetInputLayout({{"SV_POSITION", Format::R32G32B32A32_FLOAT, 0}, {"COLOR", Format::R32G32B32A32_FLOAT, 16}});
    c4.IASetVertexBuffer(vb, sizeof(V4), 0); c4.IASetPrimitiveTopology(Topology::TriangleList);
    c4.RSSetViewport({0, 0, (float)W, (float)H}); c4.OMSetRenderTarget(r4);
    CHECK(c4.Draw(3, 0));
    std::vector<uint8_t> o; d4.ReadTexture(t4, &o);
    // pixel (10,50): affine weights (0.625, 0.1640625, 0.2109375); perspective weights ~ l_i/w_i normalised
    double l0 = 0.625, l1 = 0.1640625 / 2, l2 = 0.2109375, s = l0 + l1 + l2;
    int er = (int)std::lround(l0 / s * 255), eg = (int)std::lround(l1 / s * 255), eb = (int)std::lround(l2 / s * 255);
    auto p = px(o, W, 10, 50);
    CHECK(near(p[0], er) && near(p[1], eg) && near(p[2], eb));
    CHECK(eg < 42); }   // differs from the affine 42, proving perspective correction

  // 6. Validation: bad state is refused with a message, never a crash
  { Device d5; DeviceContext c5(d5);
    CHECK(!c5.Draw(3, 0) && !c5.last_error().empty());                          // nothing bound
    CHECK(d5.CreateBuffer(nullptr, 4) == 0 && d5.CreateTexture2D(0, 4, Format::R8G8B8A8_UNORM) == 0 &&
          d5.CreateTexture2D(4, 4, Format::R32G32_FLOAT) == 0 && d5.CreateRenderTargetView(9) == 0);
    auto t5 = d5.CreateTexture2D(8, 8, Format::R8G8B8A8_UNORM); auto r5 = d5.CreateRenderTargetView(t5);
    std::vector<Vtx> v(3, Vtx{0, 0, 0, 1, 1, 1, 1});
    auto vb = d5.CreateBuffer(v.data(), v.size() * sizeof(Vtx));
    c5.IASetInputLayout(layout); c5.IASetVertexBuffer(vb, sizeof(Vtx), 0); c5.IASetPrimitiveTopology(Topology::TriangleList);
    c5.OMSetRenderTarget(r5);
    CHECK(!c5.Draw(3, 0));                                                       // no viewport
    c5.RSSetViewport({0, 0, 8, 8});
    CHECK(!c5.Draw(6, 0));                                                       // runs past buffer
    CHECK(!c5.Draw(3, 1));                                                       // start pushes it past the end
    CHECK(!c5.Draw(2, 0));                                                       // not a multiple of 3
    c5.IASetInputLayout({{"POSITION", Format::R32G32B32_FLOAT, 24}});
    CHECK(!c5.Draw(3, 0));                                                       // element does not fit stride
    c5.IASetInputLayout({{"COLOR", Format::R32G32B32_FLOAT, 0}});
    CHECK(!c5.Draw(3, 0));                                                       // no POSITION
    c5.IASetInputLayout(layout); c5.IASetPrimitiveTopology(Topology::Undefined);
    CHECK(!c5.Draw(3, 0));
    c5.IASetPrimitiveTopology(Topology::TriangleList);
    CHECK(c5.Draw(3, 0) && c5.stats().pixels_written == 0);                      // degenerate triangle draws nothing
    // huge and non-finite coordinates are dropped, not rasterized
    std::vector<Vtx> big = {{-1e9f, 0, 0, 1,1,1,1}, {1e9f, 1, 0, 1,1,1,1}, {0, NAN, 0, 1,1,1,1}};
    c5.IASetVertexBuffer(d5.CreateBuffer(big.data(), big.size() * sizeof(Vtx)), sizeof(Vtx), 0);
    CHECK(c5.Draw(3, 0) && c5.stats().pixels_written == 0); }
  // ---- indexed drawing: same image as the expanded vertex list; 16 and 32 bit; start index; base vertex; bounds ----
  {
    struct V4 { float x, y, z, w, r, g, b, a; };
    const std::vector<InputElement> lay = {{"POSITION", Format::R32G32B32A32_FLOAT, 0}, {"COLOR", Format::R32G32B32A32_FLOAT, 16}};
    V4 quad[4] = {{-0.8f, 0.8f, 0, 1, 1, 0, 0, 1}, {0.8f, 0.8f, 0, 1, 0, 1, 0, 1}, {-0.8f, -0.8f, 0, 1, 0, 0, 1, 1}, {0.8f, -0.8f, 0, 1, 1, 1, 0, 1}};
    auto mk = [&](Device& d, DeviceContext& c, ResourceId& tex) {
      tex = d.CreateTexture2D(64, 64, Format::R8G8B8A8_UNORM); c.IASetInputLayout(lay); c.IASetPrimitiveTopology(Topology::TriangleList);
      c.RSSetViewport({0, 0, 64, 64}); c.OMSetRenderTarget(d.CreateRenderTargetView(tex));
      RasterizerDesc rd; rd.cull = Cull::None; c.RSSetState(rd); };
    Device da, db, dc, dd; DeviceContext ca(da), cb(db), cc(dc), cd(dd); ResourceId ta, tb, tc, td;
    mk(da, ca, ta); mk(db, cb, tb); mk(dc, cc, tc); mk(dd, cd, td);
    V4 expanded[6] = {quad[0], quad[1], quad[2], quad[1], quad[3], quad[2]};
    ca.IASetVertexBuffer(da.CreateBuffer(expanded, sizeof expanded), sizeof(V4), 0); CHECK(ca.Draw(6, 0));
    cb.IASetVertexBuffer(db.CreateBuffer(quad, sizeof quad), sizeof(V4), 0);
    uint16_t i16[6] = {0, 1, 2, 1, 3, 2};
    cb.IASetIndexBuffer(db.CreateBuffer(i16, sizeof i16), false, 0); CHECK(cb.DrawIndexed(6, 0, 0));
    // 32-bit indices with a base vertex: vertices stored at offset +2, indices relative to it; start index 2 skips the padding
    V4 shifted[6] = {{9, 9, 0, 1, 0, 0, 0, 1}, {9, 9, 0, 1, 0, 0, 0, 1}, quad[0], quad[1], quad[2], quad[3]};
    uint32_t j32[8] = {7, 7, 0, 1, 2, 1, 3, 2};
    cc.IASetVertexBuffer(dc.CreateBuffer(shifted, sizeof shifted), sizeof(V4), 0);
    cc.IASetIndexBuffer(dc.CreateBuffer(j32, sizeof j32), true, 0); CHECK(cc.DrawIndexed(6, 2, 2));
    std::vector<uint8_t> ia, ib, ic; da.ReadTexture(ta, &ia); db.ReadTexture(tb, &ib); dc.ReadTexture(tc, &ic);
    CHECK(ia == ib); CHECK(ia == ic);
    int lit = 0; for (size_t i = 0; i < ia.size(); i += 4) if (ia[i] + ia[i + 1] + ia[i + 2] > 0) lit++;
    CHECK(lit > 1500);
    // bounds: no index buffer, range past the buffer, index past the vertex buffer, negative base vertex
    CHECK(!cd.DrawIndexed(6, 0, 0));
    cd.IASetVertexBuffer(dd.CreateBuffer(quad, sizeof quad), sizeof(V4), 0);
    cd.IASetIndexBuffer(dd.CreateBuffer(i16, sizeof i16), false, 0);
    CHECK(!cd.DrawIndexed(6, 3, 0));        // 3 + 6 indices > 6 stored
    CHECK(!cd.DrawIndexed(6, 0, 1));        // index 3 + base 1 = vertex 4, past 4 vertices
    CHECK(!cd.DrawIndexed(6, 0, -1));       // index 0 + base -1 = vertex -1
    CHECK(!cd.IASetIndexBuffer(99, false, 0));
    std::puts("indexed drawing ok"); }
  std::puts("ok"); return 0;
}
