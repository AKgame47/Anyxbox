#include "gfx/d3d11_model.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace anyxbox { namespace d3d11 {
namespace {
uint32_t fmt_floats(Format f) {
  switch (f) { case Format::R32G32_FLOAT: return 2; case Format::R32G32B32_FLOAT: return 3; case Format::R32G32B32A32_FLOAT: return 4; default: return 0; }
}
uint8_t to_u8(float v) { if (!(v > 0.f)) return 0; if (v >= 1.f) return 255; return (uint8_t)std::floor(v * 255.f + 0.5f); }
struct V { float x, y, z, w; float c[4]; };
void store(Device::Texture& t, uint32_t x, uint32_t y, const float c[4]) {
  uint8_t* p = &t.px[((size_t)y * t.w + x) * 4];
  uint8_t r = to_u8(c[0]), g = to_u8(c[1]), b = to_u8(c[2]), a = to_u8(c[3]);
  if (t.f == Format::B8G8R8A8_UNORM) { p[0] = b; p[1] = g; p[2] = r; p[3] = a; } else { p[0] = r; p[1] = g; p[2] = b; p[3] = a; }
}
}  // namespace

ResourceId Device::CreateBuffer(const void* data, size_t bytes) {
  if (!data || bytes == 0 || bytes > (size_t(1) << 28)) return 0;
  Buffer b; b.data.assign((const uint8_t*)data, (const uint8_t*)data + bytes);
  buffers_.push_back(std::move(b)); return (ResourceId)buffers_.size();
}
ResourceId Device::CreateTexture2D(uint32_t w, uint32_t h, Format f) {
  if (w == 0 || h == 0 || w > 16384 || h > 16384) return 0;
  if (f != Format::R8G8B8A8_UNORM && f != Format::B8G8R8A8_UNORM) return 0;
  Texture t; t.w = w; t.h = h; t.f = f; t.px.assign((size_t)w * h * 4, 0);
  textures_.push_back(std::move(t)); return (ResourceId)textures_.size();
}
ResourceId Device::CreateRenderTargetView(ResourceId tex) {
  if (!texture(tex)) return 0;
  rtvs_.push_back(tex); return (ResourceId)rtvs_.size();
}
const Device::Buffer* Device::buffer(ResourceId id) const { return id && id <= buffers_.size() ? &buffers_[id - 1] : nullptr; }
Device::Texture* Device::texture(ResourceId id) { return id && id <= textures_.size() ? &textures_[id - 1] : nullptr; }
const Device::Texture* Device::texture(ResourceId id) const { return id && id <= textures_.size() ? &textures_[id - 1] : nullptr; }
ResourceId Device::rtv_texture(ResourceId rtv) const { return rtv && rtv <= rtvs_.size() ? rtvs_[rtv - 1] : 0; }
bool Device::ReadTexture(ResourceId id, std::vector<uint8_t>* out) const {
  auto* t = texture(id); if (!t || !out) return false; *out = t->px; return true;
}

bool DeviceContext::ClearRenderTargetView(ResourceId rtv, const float c[4]) {
  auto* t = dev_.texture(dev_.rtv_texture(rtv));
  if (!t) return fail("clear: invalid render target view");
  for (uint32_t y = 0; y < t->h; y++) for (uint32_t x = 0; x < t->w; x++) store(*t, x, y, c);
  if (backend_ && !backend_->clear(dev_, dev_.rtv_texture(rtv), c, &err_)) return false;
  return true;
}

bool DeviceContext::Draw(uint32_t count, uint32_t start) {
  err_.clear();
  if (topo_ != Topology::TriangleList) return fail("draw: only TriangleList is supported");
  auto* rt = dev_.texture(dev_.rtv_texture(rtv_));
  if (!rt) return fail("draw: no valid render target bound");
  if (!has_vp_ || !(vp_.width > 0) || !(vp_.height > 0)) return fail("draw: viewport not set");
  auto* vb = dev_.buffer(vb_);
  if (!vb) return fail("draw: no valid vertex buffer bound");
  if (stride_ == 0 || count % 3 != 0) return fail("draw: stride is 0 or vertex count is not a multiple of 3");
  uint64_t need = (uint64_t)vb_offset_ + (uint64_t)stride_ * ((uint64_t)start + count);
  if (need > vb->data.size()) return fail("draw: vertex fetch runs past the end of the buffer");
  const InputElement* pos = nullptr; const InputElement* col = nullptr;
  for (auto& e : layout_) {
    uint32_t n = fmt_floats(e.format);
    if (!n) return fail("draw: unsupported input element format");
    if ((uint64_t)e.offset + n * 4 > stride_) return fail("draw: input element does not fit in the vertex stride");
    if (e.semantic == "POSITION" || e.semantic == "SV_POSITION") pos = &e;
    else if (e.semantic == "COLOR") col = &e;
  }
  if (!pos) return fail("draw: input layout has no POSITION element");

  auto fetch = [&](uint32_t i, V& v) {
    const uint8_t* base = vb->data.data() + vb_offset_ + (size_t)stride_ * (start + i);
    float p[4] = {0, 0, 0, 1};
    std::memcpy(p, base + pos->offset, fmt_floats(pos->format) * 4);
    v.x = p[0]; v.y = p[1]; v.z = p[2]; v.w = p[3];
    float c[4] = {1, 1, 1, 1};
    if (col) std::memcpy(c, base + col->offset, fmt_floats(col->format) * 4);
    std::memcpy(v.c, c, 16);
  };

  std::vector<V> all(count);
  for (uint32_t i = 0; i < count; i++) fetch(i, all[i]);
  if (backend_) {
    std::vector<ClipVertex> cv(count);
    for (uint32_t i = 0; i < count; i++) cv[i] = {all[i].x, all[i].y, all[i].z, all[i].w, all[i].c[0], all[i].c[1], all[i].c[2], all[i].c[3]};
    stats_.triangles_in += count / 3;
    if (!backend_->draw(dev_, dev_.rtv_texture(rtv_), vp_, cv, &stats_, &err_)) { if (err_.empty()) err_ = "draw: backend failed"; return false; }
    return true;
  }
  for (uint32_t t = 0; t < count; t += 3) {
    stats_.triangles_in++;
    V v[3]; for (int k = 0; k < 3; k++) v[k] = all[t + k];
    bool ok = true; int64_t fx[3], fy[3]; float iw[3];
    for (int k = 0; k < 3; k++) {
      if (!(v[k].w > 0.f) || !std::isfinite(v[k].x) || !std::isfinite(v[k].y) || !std::isfinite(v[k].w)) { ok = false; break; }
      float ndcx = v[k].x / v[k].w, ndcy = v[k].y / v[k].w;
      float sx = vp_.x + (ndcx + 1.f) * 0.5f * vp_.width, sy = vp_.y + (1.f - ndcy) * 0.5f * vp_.height;
      if (std::fabs(sx) > 1e6f || std::fabs(sy) > 1e6f) { ok = false; break; }
      fx[k] = (int64_t)std::llround(sx * 256.0); fy[k] = (int64_t)std::llround(sy * 256.0);   // 8-bit subpixel
      iw[k] = 1.f / v[k].w;
    }
    if (!ok) continue;
    int64_t area = (fx[1] - fx[0]) * (fy[2] - fy[0]) - (fx[2] - fx[0]) * (fy[1] - fy[0]);
    if (area == 0) continue;
    if (area < 0) { std::swap(fx[1], fx[2]); std::swap(fy[1], fy[2]); std::swap(v[1], v[2]); std::swap(iw[1], iw[2]); area = -area; }
    // bounding box in pixels, clamped to target and viewport-independent (no scissor in this slice)
    int64_t minx = std::min({fx[0], fx[1], fx[2]}) >> 8, maxx = (std::max({fx[0], fx[1], fx[2]}) + 255) >> 8;
    int64_t miny = std::min({fy[0], fy[1], fy[2]}) >> 8, maxy = (std::max({fy[0], fy[1], fy[2]}) + 255) >> 8;
    minx = std::max<int64_t>(minx, 0); miny = std::max<int64_t>(miny, 0);
    maxx = std::min<int64_t>(maxx, rt->w); maxy = std::min<int64_t>(maxy, rt->h);
    // edge i goes from vertex (i+1) to (i+2); weight for vertex i is the edge opposite it
    // (positive area = clockwise in y-down screen space after the swap above)
    bool topleft[3];
    for (int i = 0; i < 3; i++) {
      int a = (i + 1) % 3, b = (i + 2) % 3;
      int64_t dx = fx[b] - fx[a], dy = fy[b] - fy[a];
      topleft[i] = (dy == 0 && dx > 0) || dy < 0;   // clockwise in y-down space: top edge runs left to right, left edge runs bottom to top
    }
    bool drew = false;
    for (int64_t py = miny; py < maxy; py++) for (int64_t px = minx; px < maxx; px++) {
      int64_t sx = px * 256 + 128, sy = py * 256 + 128;   // pixel center
      int64_t w[3]; bool in = true;
      for (int i = 0; i < 3; i++) {
        int a = (i + 1) % 3, b = (i + 2) % 3;
        w[i] = (fx[b] - fx[a]) * (sy - fy[a]) - (fy[b] - fy[a]) * (sx - fx[a]);
        if (w[i] < 0 || (w[i] == 0 && !topleft[i])) { in = false; break; }
      }
      if (!in) continue;
      // perspective-correct interpolation
      double l[3], sum = 0;
      for (int i = 0; i < 3; i++) { l[i] = (double)w[i] / (double)area * iw[i]; sum += l[i]; }
      float c[4] = {0, 0, 0, 0};
      for (int i = 0; i < 3; i++) for (int k = 0; k < 4; k++) c[k] += (float)(l[i] / sum) * v[i].c[k];
      store(*rt, (uint32_t)px, (uint32_t)py, c);
      stats_.pixels_written++; drew = true;
    }
    if (drew) stats_.triangles_drawn++;
  }
  return true;
}
}}
