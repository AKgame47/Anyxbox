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
bool VulkanDrawBackend::clear_depth(Device&, float value, std::string* err) {
  if (value != 1.0f) { *err = "vulkan: depth can only be cleared to 1.0 for now"; return false; }
  for (auto& kv : targets_) if (kv.second.has_surface) { *err = "vulkan: depth clear after a draw is not supported (clear colour and depth before the frame's first draw)"; return false; }
  return true;
}
namespace {
int32_t vk_factor(BlendFactor f) { switch (f) { case BlendFactor::Zero: return 0; case BlendFactor::One: return 1; case BlendFactor::SrcColor: return 2;
  case BlendFactor::InvSrcColor: return 3; case BlendFactor::DestColor: return 4; case BlendFactor::InvDestColor: return 5; case BlendFactor::SrcAlpha: return 6;
  case BlendFactor::InvSrcAlpha: return 7; case BlendFactor::DestAlpha: return 8; case BlendFactor::InvDestAlpha: return 9; } return 1; }
int32_t vk_op(BlendOp o) { switch (o) { case BlendOp::Add: return 0; case BlendOp::Subtract: return 1; case BlendOp::RevSubtract: return 2; case BlendOp::Min: return 3; case BlendOp::Max: return 4; } return 0; }
int32_t vk_cmp(Compare c) { switch (c) { case Compare::Never: return 0; case Compare::Less: return 1; case Compare::Equal: return 2; case Compare::LessEqual: return 3;
  case Compare::Greater: return 4; case Compare::NotEqual: return 5; case Compare::GreaterEqual: return 6; case Compare::Always: return 7; } return 1; }
}  // namespace
bool VulkanDrawBackend::draw(Device& dev, ResourceId tex, const Viewport& vp, const std::vector<ClipVertex>& verts,
                             DrawStats* stats, std::string* err) {
  auto* rt = dev.texture(tex);
  if (!rt) { *err = "vulkan draw: invalid render target"; return false; }
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
  if (vs_ubo_ && cb0_.size() > 16384) { *err = "vulkan draw: vertex constants larger than 16384 bytes are not supported"; return false; }
  if (!cb0_.empty() && !vs_ubo_) {
    if (cb0_.size() > 128) { *err = "vulkan draw: constant buffer 0 larger than 128 bytes is not supported yet"; return false; }
    push.assign((cb0_.size() + 3) / 4, 0); std::memcpy(push.data(), cb0_.data(), cb0_.size());
  }
  if (ps_cb0_.size() > 16384) { *err = "vulkan draw: pixel constant buffer 0 larger than 16384 bytes is not supported"; return false; }
  const size_t total_tris = kept.size() / (3 * fpv), chunk = 9999;   // backend limit: 30000 indices per call
  std::vector<uint8_t> img;
  const bool use_depth = st_.depth_bound;
  const bool dtest = use_depth && st_.ds.depth_enable;
  const bool dwrite = dtest && st_.ds.depth_write;
  const int32_t dcmp = dtest ? vk_cmp(st_.ds.func) : 7;   // VK_COMPARE_OP_ALWAYS
  const int32_t cull = st_.rs.cull == Cull::None ? VK_CULL_MODE_NONE : st_.rs.cull == Cull::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
  // y is already flipped to match the screen picture, so D3D's visual winding maps straight across
  const int32_t front = st_.rs.front_ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
  BlendState bs; bs.enable = st_.blend.enable; bs.src_color = vk_factor(st_.blend.src); bs.dst_color = vk_factor(st_.blend.dst);
  bs.color_op = vk_op(st_.blend.op); bs.src_alpha = vk_factor(st_.blend.src_a); bs.dst_alpha = vk_factor(st_.blend.dst_a); bs.alpha_op = vk_op(st_.blend.op_a);
  TexInput texin; if (textured) { texin.rgba = tex_.data(); texin.w = tex_w_; texin.h = tex_h_; texin.linear = smp_.linear; texin.repeat = smp_.wrap; texin.levels = tex_levels_; texin.mip_mode = smp_.mip_linear ? 1 : 0; texin.min_lod = std::max(0.f,smp_.min_lod); texin.max_lod = std::max(0.f,smp_.max_lod); }
  std::vector<pnr::graphics::TexInput> extra_in;
  for (int xi = 0; xi < 3; xi++) {
    const Extra& e = extra_[xi]; if (e.rgba.empty()) continue;
    if (!textured) { *err = "vulkan draw: textures t1..t3 need t0 bound as well"; return false; }
    if (e.levels > 1) { *err = "vulkan draw: mip chains on t1..t3 not supported yet"; return false; }
    if (e.w > 256 || e.h > 256) { *err = "vulkan draw: textures larger than 256x256 are not supported yet"; return false; }
    if (xi != (int)extra_in.size()) { *err = "vulkan draw: bind t1, t2, t3 in order without gaps"; return false; }
    pnr::graphics::TexInput t; t.rgba = e.rgba.data(); t.w = e.w; t.h = e.h; t.linear = e.smp.linear; t.repeat = e.smp.wrap; extra_in.push_back(t);
  }
  VkViewport viewport{vp.x, vp.y, vp.width, vp.height, vp.min_depth, vp.max_depth};
  VkRect2D scissor{{0,0},{rt->w,rt->h}};
  if (st_.rs.scissor_enable) {
    auto clamp = [](int32_t x, uint32_t limit) { return (int32_t)std::max<int64_t>(0, std::min<int64_t>(x, limit)); };
    int32_t l = clamp(st_.scissor.left, rt->w), t = clamp(st_.scissor.top, rt->h);
    int32_t r = clamp(st_.scissor.right, rt->w), b = clamp(st_.scissor.bottom, rt->h);
    scissor = {{l,t},{(uint32_t)std::max(0,r-l),(uint32_t)std::max(0,b-t)}};
  }
  for (size_t first = 0; first < total_tris; first += chunk) {
    size_t n = std::min(chunk, total_tris - first);
    std::vector<float> v(kept.begin() + first * 3 * fpv, kept.begin() + (first + n) * 3 * fpv);
    std::vector<uint32_t> idx(n * 3); for (size_t i = 0; i < idx.size(); i++) idx[i] = (uint32_t)i;
    pnr::Status s = textured ? vk_.render_textured_clip_mesh_rgba(rt->w, rt->h, vs, fs, v, idx, use_depth, texin, tg.clear, &img, &tg.surf, dwrite, &bs, cull, front, dcmp, push.empty() ? nullptr : &push, ps_cb0_.empty() ? nullptr : &ps_cb0_, (vs_ubo_ && !cb0_.empty()) ? &cb0_ : nullptr, extra_in.empty() ? nullptr : &extra_in, &scissor, &viewport)
      : vk_.render_clip_mesh_rgba(rt->w, rt->h, vs, fs, v, idx, use_depth, tg.clear, &img, &tg.surf, dwrite, &bs, cull, front, dcmp, push.empty() ? nullptr : &push, ps_cb0_.empty() ? nullptr : &ps_cb0_, (vs_ubo_ && !cb0_.empty()) ? &cb0_ : nullptr, &scissor, &viewport);
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
