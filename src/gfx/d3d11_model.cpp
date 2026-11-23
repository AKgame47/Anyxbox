#include "gfx/d3d11_model.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
namespace anyxbox { namespace d3d11 {
namespace {
uint32_t fmt_floats(Format f) {
  switch (f) { case Format::R32G32_FLOAT: return 2; case Format::R32G32B32_FLOAT: return 3; case Format::R32G32B32A32_FLOAT: return 4; default: return 0; }
}
uint8_t to_u8(float v) { if (!(v > 0.f)) return 0; if (v >= 1.f) return 255; return (uint8_t)std::floor(v * 255.f + 0.5f); }
struct V { float x, y, z, w; float c[4]; float uv[2]; };
void store(Device::Texture& t, uint32_t x, uint32_t y, const float c[4]) {
  uint8_t* p = &t.px[((size_t)y * t.w + x) * 4];
  uint8_t r = to_u8(c[0]), g = to_u8(c[1]), b = to_u8(c[2]), a = to_u8(c[3]);
  if (t.f == Format::B8G8R8A8_UNORM) { p[0] = b; p[1] = g; p[2] = r; p[3] = a; } else { p[0] = r; p[1] = g; p[2] = b; p[3] = a; }
}
float factor(BlendFactor f, const float s[4], const float d[4], int ch) {
  switch (f) {
    case BlendFactor::Zero: return 0.f; case BlendFactor::One: return 1.f;
    case BlendFactor::SrcColor: return s[ch]; case BlendFactor::InvSrcColor: return 1.f - s[ch];
    case BlendFactor::SrcAlpha: return s[3]; case BlendFactor::InvSrcAlpha: return 1.f - s[3];
    case BlendFactor::DestAlpha: return d[3]; case BlendFactor::InvDestAlpha: return 1.f - d[3];
    case BlendFactor::DestColor: return d[ch]; case BlendFactor::InvDestColor: return 1.f - d[ch];
  }
  return 1.f;
}
float apply_op(BlendOp op, float a, float b) {   // a = src*fs, b = dst*fd ; Min/Max ignore the factors
  switch (op) { case BlendOp::Add: return a + b; case BlendOp::Subtract: return a - b; case BlendOp::RevSubtract: return b - a;
    case BlendOp::Min: return std::min(a, b); case BlendOp::Max: return std::max(a, b); }
  return a;
}
bool depth_pass(Compare f, float a, float b) {   // a = incoming, b = stored
  switch (f) { case Compare::Never: return false; case Compare::Less: return a < b; case Compare::Equal: return a == b;
    case Compare::LessEqual: return a <= b; case Compare::Greater: return a > b; case Compare::NotEqual: return a != b;
    case Compare::GreaterEqual: return a >= b; case Compare::Always: return true; }
  return true;
}
void store_blend(Device::Texture& t, uint32_t x, uint32_t y, const float c[4], const BlendDesc& bd) {
  if (!bd.enable) { store(t, x, y, c); return; }
  const uint8_t* p = &t.px[((size_t)y * t.w + x) * 4];
  float d[4]; bool bgr = t.f == Format::B8G8R8A8_UNORM;
  d[0] = (bgr ? p[2] : p[0]) / 255.f; d[1] = p[1] / 255.f; d[2] = (bgr ? p[0] : p[2]) / 255.f; d[3] = p[3] / 255.f;
  float s[4]; for (int k = 0; k < 4; k++) s[k] = c[k] < 0.f ? 0.f : c[k] > 1.f ? 1.f : c[k];   // output merger saturates source
  float o[4];
  for (int k = 0; k < 3; k++) o[k] = apply_op(bd.op, s[k] * factor(bd.src, s, d, k), d[k] * factor(bd.dst, s, d, k));
  o[3] = apply_op(bd.op_a, s[3] * factor(bd.src_a, s, d, 3), d[3] * factor(bd.dst_a, s, d, 3));
  if (bd.op == BlendOp::Min) for (int k = 0; k < 3; k++) o[k] = std::min(s[k], d[k]);
  if (bd.op == BlendOp::Max) for (int k = 0; k < 3; k++) o[k] = std::max(s[k], d[k]);
  if (bd.op_a == BlendOp::Min) o[3] = std::min(s[3], d[3]);
  if (bd.op_a == BlendOp::Max) o[3] = std::max(s[3], d[3]);
  store(t, x, y, o);
}
}  // namespace

ResourceId Device::CreateBuffer(const void* data, size_t bytes) {
  if (!data || bytes == 0 || bytes > (size_t(1) << 28)) return 0;
  Buffer b; b.data.assign((const uint8_t*)data, (const uint8_t*)data + bytes);
  buffers_.push_back(std::move(b)); return (ResourceId)buffers_.size();
}
ResourceId Device::CreateTexture2D(uint32_t w, uint32_t h, Format f, uint32_t levels) {
  if (w == 0 || h == 0 || w > 16384 || h > 16384) return 0;
  if (f != Format::R8G8B8A8_UNORM && f != Format::B8G8R8A8_UNORM) return 0;
  uint32_t full = 1; for (uint32_t n = std::max(w,h); n > 1; n >>= 1) ++full;
  if (!levels) levels = full;
  if (levels > full) return 0;
  size_t total=0;
  for(uint32_t l=0;l<levels;++l) total+=(size_t)std::max(1u,w>>l)*std::max(1u,h>>l)*4;
  if(total>64u*1024u*1024u) return 0;
  try {
  Texture t; for (uint32_t l=1;l<levels;++l) t.mips.emplace_back((size_t)std::max(1u,w>>l)*std::max(1u,h>>l)*4,0);
  t.w = w; t.h = h; t.f = f; t.px.assign((size_t)w * h * 4, 0);
  textures_.push_back(std::move(t)); return (ResourceId)textures_.size();
  } catch(const std::bad_alloc&) { return 0; }
}
bool Device::GenerateMips(ResourceId id, uint32_t first, uint32_t levels) {
  auto* t = texture(id); if (!t) return false;
  uint32_t total = (uint32_t)t->mips.size()+1;
  if (first >= total) return false;
  if (levels == ~0u) levels = total-first;
  if (!levels || levels > total-first) return false;
  uint32_t sw = std::max(1u,t->w>>first), sh = std::max(1u,t->h>>first);
  const std::vector<uint8_t>* src = first ? &t->mips[first-1] : &t->px;
  for (uint32_t l=first+1;l<first+levels;++l) {
    auto& dst = t->mips[l-1];
    uint32_t dw = std::max(1u,sw/2), dh = std::max(1u,sh/2);
    // Area-weighted box reduction includes the final row/column of odd dimensions.
    for(uint32_t y=0;y<dh;++y) for(uint32_t x=0;x<dw;++x) {
      double lo_x = (double)x*sw/dw, hi_x = (double)(x+1)*sw/dw;
      double lo_y = (double)y*sh/dh, hi_y = (double)(y+1)*sh/dh;
      double sum[4] = {}, weight = 0;
      for(uint32_t sy=(uint32_t)lo_y;sy<(uint32_t)std::ceil(hi_y);++sy)
        for(uint32_t sx=(uint32_t)lo_x;sx<(uint32_t)std::ceil(hi_x);++sx) {
          double w = (std::min(hi_x,(double)sx+1)-std::max(lo_x,(double)sx)) *
                     (std::min(hi_y,(double)sy+1)-std::max(lo_y,(double)sy));
          for(int k=0;k<4;++k) sum[k] += (*src)[((size_t)sy*sw+sx)*4+k]*w;
          weight += w;
        }
      for(int k=0;k<4;++k) dst[((size_t)y*dw+x)*4+k] = (uint8_t)std::lround(sum[k]/weight);
    }
    src = &dst; sw = dw; sh = dh;
  }
  return true;
}
bool Device::UpdateTextureMip(ResourceId id, uint32_t level, const void* data, size_t bytes) {
  auto* t = texture(id); if (!t || level > t->mips.size() || !data) return false;
  auto& dst = level ? t->mips[level-1] : t->px;
  if (bytes != dst.size()) return false;
  std::memcpy(dst.data(),data,bytes); return true;
}
bool Device::UpdateBuffer(ResourceId id, const void* data, size_t bytes) {
  if (id == 0 || id > buffers_.size() || !data || bytes != buffers_[id - 1].data.size()) return false;
  std::memcpy(buffers_[id - 1].data.data(), data, bytes); return true;
}
bool Device::UpdateTexture(ResourceId id, const void* data, size_t bytes) {
  Texture* t = texture(id);
  if (!t || !data || bytes != t->px.size()) return false;
  std::memcpy(t->px.data(), data, bytes); return true;
}
ResourceId Device::CreateDepthBuffer(uint32_t w, uint32_t h) {
  if (w == 0 || h == 0 || w > 16384 || h > 16384) return 0;
  Depth d; d.w = w; d.h = h; d.z.assign((size_t)w * h, 1.f);
  depths_.push_back(std::move(d)); return (ResourceId)depths_.size();
}
ResourceId Device::CreateDepthStencilView(ResourceId db) {
  if (!depth(db)) return 0;
  dsvs_.push_back(db); return (ResourceId)dsvs_.size();
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

bool DeviceContext::ClearDepthStencilView(ResourceId dsv, float depth) {
  auto* d = dev_.depth(dev_.dsv_depth(dsv));
  if (!d) return fail("clear depth: invalid depth stencil view");
  float v = depth < 0.f ? 0.f : depth > 1.f ? 1.f : depth;
  std::fill(d->z.begin(), d->z.end(), v);
  if (backend_ && !backend_->clear_depth(dev_, v, &err_)) return false;
  return true;
}

bool DeviceContext::VSSetConstantBuffer(uint32_t slot, ResourceId buffer) {
  if (slot >= 14) return fail("VSSetConstantBuffer: slot must be 0..13");
  if (buffer != 0 && !dev_.buffer(buffer)) return fail("VSSetConstantBuffer: invalid buffer");
  vs_cbs_[slot] = buffer; return true;
}

bool DeviceContext::PSSetConstantBuffer(uint32_t slot, ResourceId buffer) {
  if (slot >= 14) return fail("PSSetConstantBuffer: slot must be 0..13");
  if (buffer != 0 && !dev_.buffer(buffer)) return fail("PSSetConstantBuffer: invalid buffer");
  ps_cbs_[slot] = buffer; return true;
}

bool DeviceContext::PSSetShaderResource(uint32_t slot, ResourceId tex, uint32_t first, uint32_t levels) {
  if (slot >= 4) return fail("PSSetShaderResource: slot must be 0..3");
  if (tex != 0 && !dev_.texture(tex)) return fail("PSSetShaderResource: invalid texture");
  if (tex) {
    uint32_t total = (uint32_t)dev_.texture(tex)->mips.size()+1;
    if (first >= total) return fail("PSSetShaderResource: mip start outside texture");
    if (levels == ~0u) levels = total-first;
    if (!levels || levels > total-first) return fail("PSSetShaderResource: invalid mip count");
  }
  ps_texs_[slot] = tex; ps_first_[slot] = first; ps_levels_[slot] = levels; return true;
}
bool DeviceContext::PSSetSampler(uint32_t slot, const SamplerDesc& s) {
  if (slot >= 4) return fail("PSSetSampler: slot must be 0..3");
  ps_smps_[slot] = s; return true;
}

bool DeviceContext::Draw(uint32_t count, uint32_t start) { return draw_impl(count, start, false, 0); }
bool DeviceContext::DrawIndexed(uint32_t count, uint32_t start, int32_t base) { return draw_impl(count, start, true, base); }
bool DeviceContext::IASetIndexBuffer(ResourceId buffer, bool index32, uint32_t offset) {
  if (buffer != 0 && !dev_.buffer(buffer)) return fail("IASetIndexBuffer: invalid buffer");
  ib_ = buffer; ib32_ = index32; ib_offset_ = offset; return true;
}
bool DeviceContext::draw_impl(uint32_t count, uint32_t start, bool indexed, int32_t base_vertex) {
  err_.clear();
  if (topo_ != Topology::TriangleList) return fail("draw: only TriangleList is supported");
  auto* rt = dev_.texture(dev_.rtv_texture(rtv_));
  if (!rt) return fail("draw: no valid render target bound");
  if (!has_vp_ || !(vp_.width > 0) || !(vp_.height > 0)) return fail("draw: viewport not set");
  if (!std::isfinite(vp_.x) || !std::isfinite(vp_.y) || !std::isfinite(vp_.width) || !std::isfinite(vp_.height) ||
      !std::isfinite(vp_.min_depth) || !std::isfinite(vp_.max_depth) || vp_.min_depth < 0 || vp_.max_depth > 1 || vp_.min_depth > vp_.max_depth)
    return fail("draw: invalid viewport");
  Device::Depth* depth = nullptr;
  if (dsv_) {
    depth = dev_.depth(dev_.dsv_depth(dsv_));
    if (!depth) return fail("draw: invalid depth stencil view bound");
    if (depth->w != rt->w || depth->h != rt->h) return fail("draw: depth buffer size differs from the render target");
  }
  state_.depth_bound = depth != nullptr;
  if (count % 3 != 0) return fail("draw: vertex count is not a multiple of 3");
  // every input element reads from its own vertex buffer slot
  const Device::Buffer* slot_buf[16] = {};
  for (auto& e : layout_) {
    if (e.slot >= 16) return fail("draw: input element slot must be 0..15");
    const VBind& vbd = vbs_[e.slot];
    slot_buf[e.slot] = dev_.buffer(vbd.buf);
    if (!slot_buf[e.slot]) return fail("draw: no valid vertex buffer bound for an input element's slot");
    if (vbd.stride == 0) return fail("draw: stride is 0");
  }
  const Device::Buffer* ib = nullptr;
  if (indexed) {
    ib = dev_.buffer(ib_);
    if (!ib) return fail("draw: no valid index buffer bound");
    uint64_t isz = ib32_ ? 4 : 2;
    if ((uint64_t)ib_offset_ + isz * ((uint64_t)start + count) > ib->data.size()) return fail("draw: index fetch runs past the end of the index buffer");
  } else {
    for (unsigned k = 0; k < 16; k++) if (slot_buf[k] && (uint64_t)vbs_[k].offset + (uint64_t)vbs_[k].stride * ((uint64_t)start + count) > slot_buf[k]->data.size())
      return fail("draw: vertex fetch runs past the end of the buffer");
  }
  // vertex index for each of the `count` fetches, validated against the vertex buffer
  std::vector<uint32_t> vidx(count);
  for (uint32_t i = 0; i < count; i++) {
    int64_t v;
    if (indexed) {
      const uint8_t* q = ib->data.data() + ib_offset_ + (size_t)(ib32_ ? 4 : 2) * (start + i);
      uint32_t raw = 0; std::memcpy(&raw, q, ib32_ ? 4 : 2);
      v = (int64_t)raw + base_vertex;
    } else v = (int64_t)start + i;
    if (v < 0) return fail("draw: vertex fetch runs past the end of the buffer");
    for (unsigned k = 0; k < 16; k++) if (slot_buf[k] && (uint64_t)vbs_[k].offset + (uint64_t)vbs_[k].stride * ((uint64_t)v + 1) > slot_buf[k]->data.size())
      return fail("draw: vertex fetch runs past the end of the buffer");
    vidx[i] = (uint32_t)v;
  }
  const InputElement* pos = nullptr; const InputElement* col = nullptr; const InputElement* tc = nullptr;
  for (auto& e : layout_) {
    uint32_t n = fmt_floats(e.format);
    if (!n) return fail("draw: unsupported input element format");
    if ((uint64_t)e.offset + n * 4 > vbs_[e.slot].stride) return fail("draw: input element does not fit in the vertex stride");
    if (e.semantic == "POSITION" || e.semantic == "SV_POSITION") pos = &e;
    else if (e.semantic == "COLOR") col = &e;
    else if (e.semantic == "TEXCOORD") tc = &e;
  }
  if (!pos) return fail("draw: input layout has no POSITION element");

  auto fetch = [&](uint32_t i, V& v) {
    auto at = [&](const InputElement* e) { return slot_buf[e->slot]->data.data() + vbs_[e->slot].offset + (size_t)vbs_[e->slot].stride * vidx[i] + e->offset; };
    float p[4] = {0, 0, 0, 1};
    std::memcpy(p, at(pos), fmt_floats(pos->format) * 4);
    v.x = p[0]; v.y = p[1]; v.z = p[2]; v.w = p[3];
    float c[4] = {1, 1, 1, 1};
    if (col) std::memcpy(c, at(col), fmt_floats(col->format) * 4);
    std::memcpy(v.c, c, 16);
    float t[4] = {0, 0, 0, 0};
    if (tc) std::memcpy(t, at(tc), fmt_floats(tc->format) * 4);
    v.uv[0] = t[0]; v.uv[1] = t[1];
  };

  std::vector<V> all(count);
  for (uint32_t i = 0; i < count; i++) fetch(i, all[i]);
  if (backend_) {
    std::vector<ClipVertex> cv(count);
    for (uint32_t i = 0; i < count; i++) cv[i] = {all[i].x, all[i].y, all[i].z, all[i].w, all[i].c[0], all[i].c[1], all[i].c[2], all[i].c[3], all[i].uv[0], all[i].uv[1]};
    stats_.triangles_in += count / 3;
    backend_->set_pipeline_state(state_);
    auto pack = [&](const ResourceId* slots, const std::vector<std::pair<uint32_t, uint32_t>>& layout) {
      std::vector<uint8_t> out;
      if (layout.empty()) { const Device::Buffer* cb = slots[0] ? dev_.buffer(slots[0]) : nullptr; if (cb) out = cb->data; return out; }
      for (auto& sl : layout) {
        size_t at = out.size(); out.resize(at + (size_t)sl.second * 16, 0);
        const Device::Buffer* cb = sl.first < 14 && slots[sl.first] ? dev_.buffer(slots[sl.first]) : nullptr;
        if (cb) std::memcpy(out.data() + at, cb->data.data(), std::min(cb->data.size(), (size_t)sl.second * 16));
      }
      return out;
    };
    backend_->set_vs_constants(pack(vs_cbs_, vs_layout_));
    backend_->set_ps_constants(pack(ps_cbs_, ps_layout_));
    int top_used = -1; for (int q = 0; q < 4; q++) if (tex_pair_[q] >= 0) top_used = q;   // highest texture slot the shader samples
    for (uint32_t ts = 0; ts < 4; ts++) {
      const SamplerDesc& smp = ps_smps_[tex_pair_[ts] >= 0 ? tex_pair_[ts] : ts];
      const Device::Texture* tx = ps_texs_[ts] ? dev_.texture(ps_texs_[ts]) : nullptr;
      std::vector<uint8_t> rgba;
      if (tx) {
        rgba = ps_first_[ts] ? tx->mips[ps_first_[ts]-1] : tx->px;
        if (tx->f == Format::B8G8R8A8_UNORM) for (size_t i = 0; i + 3 < rgba.size(); i += 4) std::swap(rgba[i], rgba[i + 2]);
      }
      uint32_t tw = tx ? std::max(1u,tx->w>>ps_first_[ts]) : 0, th = tx ? std::max(1u,tx->h>>ps_first_[ts]) : 0;
      if (!tx && (int)ts <= top_used) { rgba.assign(4, 0); tw = th = 1; }   // an unbound slot that the shader (or a higher slot) needs reads as zeros, like D3D
      const uint8_t* data = rgba.empty() ? nullptr : rgba.data();
      if (ts == 0) backend_->set_ps_texture(data, tw, th, smp); else backend_->set_ps_texture_slot(ts, data, tw, th, smp);
      std::vector<uint8_t> chain = rgba;
      uint32_t levels = tx ? ps_levels_[ts] : 1;
      if (tx) for (uint32_t l=ps_first_[ts]+1;l<ps_first_[ts]+levels;++l) {
        auto mip = tx->mips[l-1];
        if (tx->f == Format::B8G8R8A8_UNORM) for(size_t k=0;k<mip.size();k+=4) std::swap(mip[k],mip[k+2]);
        chain.insert(chain.end(),mip.begin(),mip.end());
      }
      backend_->set_ps_mip_chain(ts,chain,levels);
    }
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
    {   // culling. area > 0 means clockwise on screen (y down). Front = clockwise unless FrontCounterClockwise.
      bool cw = area > 0, front = state_.rs.front_ccw ? !cw : cw;
      if ((state_.rs.cull == Cull::Back && !front) || (state_.rs.cull == Cull::Front && front)) continue;
    }
    if (area < 0) { std::swap(fx[1], fx[2]); std::swap(fy[1], fy[2]); std::swap(v[1], v[2]); std::swap(iw[1], iw[2]); area = -area; }
    // Bounding box clamped to target and optional half-open scissor rectangle.
    int64_t minx = std::min({fx[0], fx[1], fx[2]}) >> 8, maxx = (std::max({fx[0], fx[1], fx[2]}) + 255) >> 8;
    int64_t miny = std::min({fy[0], fy[1], fy[2]}) >> 8, maxy = (std::max({fy[0], fy[1], fy[2]}) + 255) >> 8;
    minx = std::max<int64_t>(minx, 0); miny = std::max<int64_t>(miny, 0);
    maxx = std::min<int64_t>(maxx, rt->w); maxy = std::min<int64_t>(maxy, rt->h);
    if (state_.rs.scissor_enable) {
      minx = std::max<int64_t>(minx, state_.scissor.left); miny = std::max<int64_t>(miny, state_.scissor.top);
      maxx = std::min<int64_t>(maxx, state_.scissor.right); maxy = std::min<int64_t>(maxy, state_.scissor.bottom);
    }
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
      if (depth) {
        float zi = 0;
        for (int i = 0; i < 3; i++) zi += (float)((double)w[i] / (double)area) * (v[i].z / v[i].w);
        if (!(zi >= 0.f && zi <= 1.f)) continue;   // coarse z clip (no real clipping yet)
        zi = vp_.min_depth + zi * (vp_.max_depth - vp_.min_depth);
        float& stored = depth->z[(size_t)py * depth->w + px];
        if (state_.ds.depth_enable) {
          if (!depth_pass(state_.ds.func, zi, stored)) continue;
          if (state_.ds.depth_write) stored = zi;
        }
      }
      store_blend(*rt, (uint32_t)px, (uint32_t)py, c, state_.blend);
      stats_.pixels_written++; drew = true;
    }
    if (drew) stats_.triangles_drawn++;
  }
  return true;
}
}}
