// The same D3D11-model calls through the Vulkan backend and through the reference renderer must produce
// the same pixels. Skips (exit 0) when the host has no Vulkan loader or device.
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include "gfx/d3d11_vulkan.h"
using namespace anyxbox::d3d11;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)

static const int W = 128, H = 128;
static const std::vector<InputElement> kLayout = {{"POSITION", Format::R32G32B32A32_FLOAT, 0}, {"COLOR", Format::R32G32B32A32_FLOAT, 16}};
struct V { float x, y, z, w, r, g, b, a; };

// One scenario run against a context, so the same code drives both paths.
struct Rig {
  Device dev; DeviceContext ctx; ResourceId tex = 0, rtv = 0;
  explicit Rig(Format f = Format::R8G8B8A8_UNORM, DrawBackend* be = nullptr) : ctx(dev) {
    tex = dev.CreateTexture2D(W, H, f); rtv = dev.CreateRenderTargetView(tex);
    if (be) ctx.SetDrawBackend(be);
    ctx.IASetInputLayout(kLayout); ctx.IASetPrimitiveTopology(Topology::TriangleList);
    ctx.RSSetViewport({0, 0, (float)W, (float)H}); ctx.OMSetRenderTarget(rtv);
  }
  bool draw(const std::vector<V>& v) {
    ctx.IASetVertexBuffer(dev.CreateBuffer(v.data(), v.size() * sizeof(V)), sizeof(V), 0);
    return ctx.Draw((uint32_t)v.size(), 0);
  }
  std::vector<uint8_t> image() { std::vector<uint8_t> o; dev.ReadTexture(tex, &o); return o; }
};
static int mismatches(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int tol = 3) {
  int n = 0;
  for (size_t i = 0; i + 3 < a.size(); i += 4) { int d = 0; for (int k = 0; k < 3; k++) d = std::max(d, std::abs((int)a[i + k] - (int)b[i + k])); if (d > tol) n++; }
  return n;
}

int main() {
  pnr::graphics::VulkanBackend vk;
  auto s = vk.open();
  if (!s) { std::printf("SKIP: %s\n", s.error.message.c_str()); return 0; }
  s = vk.create_instance("anyxbox-test");
  if (!s || vk.devices().empty()) { std::printf("SKIP: no Vulkan instance or device\n"); return 0; }
  if (!(s = vk.create_device(0))) { std::printf("SKIP: %s\n", s.error.message.c_str()); return 0; }
  std::printf("device: %s\n", vk.device_name().c_str());
  VulkanDrawBackend be(vk);
  const float navy[4] = {0.02f, 0.03f, 0.2f, 1}, green[4] = {0, 0.6f, 0, 1};

  // 1. clear + colour triangle
  { Rig r(Format::R8G8B8A8_UNORM, &be), ref;
    std::vector<V> t = {{-0.8f,-0.8f,0,1, 1,0,0,1}, {0.8f,-0.8f,0,1, 0,1,0,1}, {0,0.8f,0,1, 0,0,1,1}};
    r.ctx.ClearRenderTargetView(r.rtv, navy); ref.ctx.ClearRenderTargetView(ref.rtv, navy);
    CHECK(r.draw(t) && ref.draw(t));
    auto a = r.image(), b = ref.image();
    std::printf("triangle: mismatches %d\n", mismatches(a, b));
    CHECK(mismatches(a, b) == 0);
    // orientation: blue vertex (y=+0.8, D3D up) is at the top of the image
    CHECK(a[(size_t)(30 * W + 64) * 4 + 2] > 150 && a[(size_t)(100 * W + 64) * 4 + 2] < 60 + 40); }

  // 2. two draws accumulate (later triangle overwrites, no depth)
  { Rig r(Format::R8G8B8A8_UNORM, &be), ref;
    std::vector<V> a1 = {{-1,-1,0,1, 1,0,0,1}, {1,-1,0,1, 1,0,0,1}, {-1,1,0,1, 1,0,0,1}};
    std::vector<V> a2 = {{-0.5f,-0.9f,0,1, 1,1,0,1}, {0.9f,-0.9f,0,1, 1,1,0,1}, {0.9f,0.5f,0,1, 0,1,1,1}};
    for (Rig* x : {&r, &ref}) { x->ctx.ClearRenderTargetView(x->rtv, navy); CHECK(x->draw(a1)); CHECK(x->draw(a2)); }
    auto a = r.image(), b = ref.image();
    std::printf("two draws: mismatches %d\n", mismatches(a, b)); CHECK(mismatches(a, b) == 0); }

  // 3. clear in the middle resets the target
  { Rig r(Format::R8G8B8A8_UNORM, &be);
    std::vector<V> big = {{-1,-1,0,1, 1,1,1,1}, {1,-1,0,1, 1,1,1,1}, {-1,1,0,1, 1,1,1,1}};
    std::vector<V> tiny = {{0.6f,0.6f,0,1, 1,0,0,1}, {0.9f,0.6f,0,1, 1,0,0,1}, {0.6f,0.9f,0,1, 1,0,0,1}};
    r.ctx.ClearRenderTargetView(r.rtv, navy); CHECK(r.draw(big));
    r.ctx.ClearRenderTargetView(r.rtv, green); CHECK(r.draw(tiny));
    auto a = r.image(); auto* p = &a[(size_t)(100 * W + 20) * 4];     // where `big` was, far from `tiny`
    CHECK(p[0] == 0 && p[1] == 153 && p[2] == 0); }

  // 4. BGRA target keeps colours right
  { Rig r(Format::B8G8R8A8_UNORM, &be), ref(Format::B8G8R8A8_UNORM);
    std::vector<V> t = {{-1,-1,0,1, 1,0,0,1}, {1,-1,0,1, 1,0,0,1}, {-1,1,0,1, 1,0,0,1}};
    for (Rig* x : {&r, &ref}) { x->ctx.ClearRenderTargetView(x->rtv, navy); CHECK(x->draw(t)); }
    auto a = r.image(), b = ref.image();
    CHECK(mismatches(a, b) == 0); const uint8_t* p = &a[(size_t)(100 * W + 10) * 4]; CHECK(p[2] == 255 && p[0] == 0); }

  // 5. many triangles (chunking over the backend's per-call limit), shared edges exact
  { Rig r(Format::R8G8B8A8_UNORM, &be), ref;
    const int GX = 100, GY = 52;   // 10400 triangles
    std::vector<V> grid;
    auto vert = [&](int i, int j) { float u = (float)i / GX, v = (float)j / GY; return V{u * 2 - 1, v * 2 - 1, 0, 1, u, v, 1 - u, 1}; };
    for (int j = 0; j < GY; j++) for (int i = 0; i < GX; i++) {
      V a = vert(i, j), b = vert(i + 1, j), c = vert(i, j + 1), d = vert(i + 1, j + 1);
      grid.insert(grid.end(), {a, b, c, b, d, c});
    }
    CHECK(grid.size() == 31200);
    for (Rig* x : {&r, &ref}) { x->ctx.ClearRenderTargetView(x->rtv, navy); CHECK(x->draw(grid)); }
    auto a = r.image(), b = ref.image();
    std::printf("grid of %zu triangles: mismatches %d\n", grid.size() / 3, mismatches(a, b)); CHECK(mismatches(a, b) == 0); }

  // 6. dropped triangles and unsupported state
  { Rig r(Format::R8G8B8A8_UNORM, &be);
    std::vector<V> bad = {{-1,-1,0,-1, 1,1,1,1}, {1,-1,0,1, 1,1,1,1}, {0,1,0,1, 1,1,1,1}};   // w < 0
    r.ctx.ClearRenderTargetView(r.rtv, navy);
    CHECK(r.draw(bad));
    auto a = r.image(); CHECK(a[0] == 5 && a[1] == 8);                                          // still the clear colour
    r.ctx.RSSetViewport({0, 0, 64, 64});
    CHECK(r.draw({{-1,-1,0,1, 1,1,1,1}, {1,-1,0,1, 1,1,1,1}, {0,1,0,1, 1,1,1,1}}));
    CHECK(r.ctx.last_error().empty()); }
  std::puts("ok"); return 0;
}
