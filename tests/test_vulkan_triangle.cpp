// Renders the colour triangle through the Vulkan backend (software Vulkan / lavapipe when no GPU is
// present) and compares every pixel against the D3D11-model reference renderer.
// Skips (exit 0, prints SKIP) when the host has no Vulkan loader or device: graphics is then Blocked, not faked.
#include <cmath>
#include <cstdio>
#include <vector>
#include "gfx/d3d11_model.h"
#include "gfx/shaders_passthrough.hpp"
#include "gfx/vulkan.hpp"
using namespace pnr::graphics;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)

int main() {
  const int W = 256, H = 256;
  // D3D-style clip space is y up; the Vulkan backend draws y down, so the translation negates y.
  const float tri[3][2] = {{-0.8f, -0.8f}, {0.8f, -0.8f}, {0.0f, 0.8f}};
  const float col[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  const float clear[4] = {0.05f, 0.05f, 0.08f, 1};

  VulkanBackend vk;
  auto s = vk.open();
  if (!s) { std::printf("SKIP: %s\n", s.error.message.c_str()); return 0; }
  s = vk.create_instance("anyxbox-test");
  if (!s || vk.devices().empty()) { std::printf("SKIP: no Vulkan instance or device\n"); return 0; }
  s = vk.create_device(0);
  if (!s) { std::printf("SKIP: %s\n", s.error.message.c_str()); return 0; }
  std::printf("device: %s%s\n", vk.device_name().c_str(), vk.devices()[0].software ? " (software)" : "");

  std::vector<float> v;
  for (int i = 0; i < 3; i++) { v.insert(v.end(), {tri[i][0], -tri[i][1], 0.f, 1.f, col[i][0], col[i][1], col[i][2]}); }
  std::vector<uint32_t> idx = {0, 1, 2};
  std::vector<uint32_t> vs(std::begin(clip_vert), std::end(clip_vert)), fs(std::begin(spin_frag), std::end(spin_frag));
  std::vector<uint8_t> vkimg;
  s = vk.render_clip_mesh_rgba(W, H, vs, fs, v, idx, false, clear, &vkimg);
  CHECK(s.ok);
  CHECK(vkimg.size() == (size_t)W * H * 4);

  // reference
  using namespace anyxbox::d3d11;
  Device d; DeviceContext c(d);
  auto tex = d.CreateTexture2D(W, H, Format::R8G8B8A8_UNORM); auto rtv = d.CreateRenderTargetView(tex);
  c.ClearRenderTargetView(rtv, clear);
  float rv[21]; for (int i = 0; i < 3; i++) { float* p = rv + i * 7; p[0] = tri[i][0]; p[1] = tri[i][1]; p[2] = 0; p[3] = col[i][0]; p[4] = col[i][1]; p[5] = col[i][2]; p[6] = 1; }
  c.IASetInputLayout({{"POSITION", Format::R32G32B32_FLOAT, 0}, {"COLOR", Format::R32G32B32A32_FLOAT, 12}});
  c.IASetVertexBuffer(d.CreateBuffer(rv, sizeof rv), 28, 0); c.IASetPrimitiveTopology(Topology::TriangleList);
  c.RSSetViewport({0, 0, (float)W, (float)H}); c.OMSetRenderTarget(rtv);
  CHECK(c.Draw(3, 0));
  std::vector<uint8_t> ref; d.ReadTexture(tex, &ref);

  // screen-space triangle for the edge band
  double sx[3], sy[3];
  for (int i = 0; i < 3; i++) { sx[i] = (tri[i][0] + 1) * 0.5 * W; sy[i] = (1 - tri[i][1]) * 0.5 * H; }
  auto edge_dist = [&](double px, double py) {
    double best = 1e9;
    for (int i = 0; i < 3; i++) {
      int j = (i + 1) % 3; double dx = sx[j] - sx[i], dy = sy[j] - sy[i];
      double t = ((px - sx[i]) * dx + (py - sy[i]) * dy) / (dx * dx + dy * dy); t = t < 0 ? 0 : t > 1 ? 1 : t;
      double ex = sx[i] + t * dx - px, ey = sy[i] + t * dy - py; double dd = std::sqrt(ex * ex + ey * ey);
      if (dd < best) best = dd;
    }
    return best;
  };
  int inner_bad = 0, edge_bad = 0, inner_total = 0, edge_total = 0, vk_cov = 0, ref_cov = 0, max_inner = 0;
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    const uint8_t* a = &vkimg[((size_t)y * W + x) * 4]; const uint8_t* b = &ref[((size_t)y * W + x) * 4];
    int diff = 0; for (int k = 0; k < 3; k++) diff = std::max(diff, std::abs((int)a[k] - (int)b[k]));
    bool vk_bg = std::abs(a[0] - 13) <= 1 && std::abs(a[1] - 13) <= 1 && std::abs(a[2] - 20) <= 1;
    bool rf_bg = std::abs(b[0] - 13) <= 1 && std::abs(b[1] - 13) <= 1 && std::abs(b[2] - 20) <= 1;
    vk_cov += !vk_bg; ref_cov += !rf_bg;
    if (edge_dist(x + 0.5, y + 0.5) <= 1.0) { edge_total++; if (diff > 3) edge_bad++; }
    else { inner_total++; if (diff > 3) inner_bad++; if (diff > max_inner) max_inner = diff; }
  }
  std::printf("coverage: vulkan %d px, reference %d px\n", vk_cov, ref_cov);
  std::printf("interior pixels: %d, mismatches (>3/255): %d, max diff %d\n", inner_total, inner_bad, max_inner);
  std::printf("edge-band pixels: %d, mismatches: %d\n", edge_total, edge_bad);
  CHECK(inner_bad == 0);                                   // away from edges every pixel agrees
  CHECK(std::abs(vk_cov - ref_cov) <= edge_total / 4);     // coverage differs only by edge-pixel choices
  CHECK(ref_cov > 20000);
  std::puts("ok"); return 0;
}
