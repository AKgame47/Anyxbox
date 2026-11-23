#include "gfx/d3d11_vulkan.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "gfx/shaders_passthrough.hpp"
namespace anyxbox { namespace d3d11 {
using namespace pnr::graphics;

VulkanDrawBackend::~VulkanDrawBackend() {
  for (auto& kv : targets_) if (kv.second.has_surface) vk_.destroy_frame(kv.second.surf);
}
bool VulkanDrawBackend::clear(Device&, ResourceId tex, const float c[4], std::string*) {
  Target& t = targets_[tex];
  if (t.has_surface) { vk_.destroy_frame(t.surf); t.has_surface = false; }
  std::memcpy(t.clear, c, sizeof t.clear);
  return true;
}
bool VulkanDrawBackend::draw(Device& dev, ResourceId tex, const Viewport& vp, const std::vector<ClipVertex>& verts,
                             DrawStats* stats, std::string* err) {
  auto* rt = dev.texture(tex);
  if (!rt) { *err = "vulkan draw: invalid render target"; return false; }
  if (vp.x != 0.f || vp.y != 0.f || vp.width != (float)rt->w || vp.height != (float)rt->h) {
    *err = "vulkan draw: viewport must cover the whole render target (not supported yet)"; return false;
  }
  const bool custom = !vs_.empty() && !fs_.empty();   // translated shaders flip y themselves (dxbc_to_spirv vulkan_flip_y)
  const bool textured = !tex_.empty();
  if (textured && !custom) { *err = "vulkan draw: a texture is bound but no shaders are set (the pass-through shaders cannot sample)"; return false; }
  if (textured && (tex_w_ > 256 || tex_h_ > 256)) { *err = "vulkan draw: textures larger than 256x256 are not supported yet"; return false; }
  const size_t fpv = textured ? 9 : 7;   // floats per vertex
  // keep only triangles the reference renderer would also draw
  std::vector<float> kept;
  for (size_t t = 0; t + 2 < verts.size(); t += 3) {
    bool ok = true;
    for (int k = 0; k < 3; k++) {
      const ClipVertex& v = verts[t + k];
      if (!(v.w > 0.f) || !std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(v.w)) ok = false;
    }
    if (!ok) continue;
    for (int k = 0; k < 3; k++) {
      const ClipVertex& v = verts[t + k];
      kept.insert(kept.end(), {v.x, custom ? v.y : -v.y, v.z, v.w, v.r, v.g, v.b});
      if (textured) kept.insert(kept.end(), {v.u, v.v});
    }
  }
  if (kept.empty()) return true;
  Target& tg = targets_[tex];
  std::vector<uint32_t> vs(std::begin(clip_vert), std::end(clip_vert)), fs(std::begin(spin_frag), std::end(spin_frag));
  if (!vs_.empty() && !fs_.empty()) { vs = vs_; fs = fs_; }
  std::vector<uint32_t> push;
  if (!cb0_.empty()) {
    if (cb0_.size() > 128) { *err = "vulkan draw: constant buffer 0 larger than 128 bytes is not supported yet"; return false; }
    push.assign((cb0_.size() + 3) / 4, 0); std::memcpy(push.data(), cb0_.data(), cb0_.size());
  }
  if (ps_cb0_.size() > 16384) { *err = "vulkan draw: pixel constant buffer 0 larger than 16384 bytes is not supported"; return false; }
  const size_t total_tris = kept.size() / (3 * fpv), chunk = 9999;   // backend limit: 30000 indices per call
  std::vector<uint8_t> img;
  TexInput texin; if (textured) { texin.rgba = tex_.data(); texin.w = tex_w_; texin.h = tex_h_; texin.linear = smp_.linear; texin.repeat = smp_.wrap; }
  for (size_t first = 0; first < total_tris; first += chunk) {
    size_t n = std::min(chunk, total_tris - first);
    std::vector<float> v(kept.begin() + first * 3 * fpv, kept.begin() + (first + n) * 3 * fpv);
    std::vector<uint32_t> idx(n * 3); for (size_t i = 0; i < idx.size(); i++) idx[i] = (uint32_t)i;
    pnr::Status s = textured ? vk_.render_textured_clip_mesh_rgba(rt->w, rt->h, vs, fs, v, idx, false, texin, tg.clear, &img, &tg.surf, true, nullptr, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_COMPARE_OP_LESS, push.empty() ? nullptr : &push, ps_cb0_.empty() ? nullptr : &ps_cb0_)
      : vk_.render_clip_mesh_rgba(rt->w, rt->h, vs, fs, v, idx, false, tg.clear, &img, &tg.surf, true, nullptr, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_COMPARE_OP_LESS, push.empty() ? nullptr : &push, ps_cb0_.empty() ? nullptr : &ps_cb0_);
    if (!s) { *err = "vulkan draw: " + s.error.message; return false; }
    tg.has_surface = true;
  }
  if (img.size() != rt->px.size()) { *err = "vulkan draw: unexpected image size"; return false; }
  rt->px = img;
  if (rt->f == Format::B8G8R8A8_UNORM) for (size_t i = 0; i < rt->px.size(); i += 4) std::swap(rt->px[i], rt->px[i + 2]);
  stats->triangles_drawn += total_tris;
  return true;
}
}}
