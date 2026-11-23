// Anyxbox: Vulkan draw path for the D3D11-style model. GPL-2.0.
// Plugs into DeviceContext::SetDrawBackend. Draws accumulate into a persistent per-render-target
// Vulkan frame surface; Clear resets it. Output is read back into the model's texture after every draw.
//
// Translation rules applied here:
//  - y is negated: D3D clip space is y up, the Vulkan backend draws y down.
//    NOTE for culling: negating y reverses triangle winding. Culling is not supported yet; when it lands,
//    front-face must be flipped to compensate.
//  - Triangles with any w <= 0 or non-finite coordinate are dropped before submission (same as the
//    reference renderer; real clipping comes later).
// Current limits (reported as errors, never silently ignored): one viewport/scissor rectangle; vertex alpha is not carried (output alpha is 1); depth/blend/cull see set_pipeline_state.
#pragma once
#include <map>
#include <utility>
#include <vector>
#include "gfx/d3d11_model.h"
#include "gfx/vulkan.hpp"
namespace anyxbox { namespace d3d11 {
class VulkanDrawBackend : public DrawBackend {
 public:
  explicit VulkanDrawBackend(pnr::graphics::VulkanBackend& vk) : vk_(vk) {}
  ~VulkanDrawBackend() override;
  bool clear(Device& dev, ResourceId tex, const float rgba[4], std::string* err) override;
  bool draw(Device& dev, ResourceId tex, const Viewport& vp, const std::vector<ClipVertex>& verts,
            DrawStats* stats, std::string* err) override;
 void set_pipeline_state(const PipelineState& st) override { st_ = st; }
 // Depth can only be cleared to 1.0 and only before the frame's first draw (the depth image is created and cleared
 // with the colour image). Blending, culling and depth test/write are honoured. Vertex alpha is not carried: blend with
 // alpha taken from a pixel-shader cbuffer or texture.
 bool clear_depth(Device& dev, float value, std::string* err) override;
 // Replace the pass-through shaders with translated SPIR-V (see shader/dxbc_spirv.h for the interface rules).
  // The vertex layout stays fixed: location 0 = float4 clip position, location 1 = float3 colour.
  // Empty vectors restore the pass-through pair. The vertex shader must come from dxbc_to_spirv with vulkan_flip_y
  // (the default): with custom shaders the backend no longer negates input y, the shader negates its output y.
  // vs_ubo: pass SpirvResult::vs_ubo of the vertex shader. When true the VS constants (all slots packed, see
  // shader/dxbc_spirv.h, up to 16384 bytes) go to a vertex uniform buffer (set 1 binding 1) instead of push constants.
  void SetShaders(std::vector<uint32_t> vs, std::vector<uint32_t> fs, bool vs_ubo = false) { vs_ = std::move(vs); fs_ = std::move(fs); vs_ubo_ = vs_ubo; }
 // cb0 contents are pushed to the vertex shader as push constants (max 128 bytes = 8 float4 registers; the Vulkan
 // guaranteed minimum). Larger buffers make the draw fail with an error rather than truncating.
 void set_vs_constants(const std::vector<uint8_t>& cb0) override { cb0_ = cb0; }
 // cb0 for the pixel shader is bound as a Vulkan uniform buffer (set 1, binding 0), up to 16384 bytes.
 void set_ps_constants(const std::vector<uint8_t>& cb0) override { ps_cb0_ = cb0; }
 // Texture t0 + sampler s0 are bound as a combined image sampler at set 0, binding 0 (RGBA8, up to 256x256, optional packed mip chain).
 // TEXCOORD (float2) travels as vertex attribute location 2. Needs custom shaders (SetShaders): the pass-through
 // pair cannot sample, so drawing with a texture bound and no shaders is an error.
 void set_ps_texture(const uint8_t* rgba, uint32_t w, uint32_t h, const SamplerDesc& s) override {
   tex_.clear(); if (rgba) tex_.assign(rgba, rgba + (size_t)w * h * 4);
   tex_w_ = w; tex_h_ = h; smp_ = s; }
 // t1..t3: 2D RGBA8 with optional mip chains, up to 256x256, set 0 bindings 1..3. They need t0 bound as well.
 void set_ps_texture_slot(uint32_t slot, const uint8_t* rgba, uint32_t w, uint32_t h, const SamplerDesc& s) override {
   if (slot < 1 || slot > 3) return;
   Extra& e = extra_[slot - 1]; e.rgba.clear(); if (rgba) e.rgba.assign(rgba, rgba + (size_t)w * h * 4);
   e.w = w; e.h = h; e.smp = s; }
 void set_ps_mip_chain(uint32_t slot, const std::vector<uint8_t>& bytes, uint32_t levels) override {
   if (slot == 0) { tex_ = bytes; tex_levels_ = levels; }
   else if (slot < 4) { extra_[slot-1].levels = levels; extra_[slot-1].rgba = bytes; }
 }
 private:
  uint32_t tex_levels_ = 1;
  struct Extra { std::vector<uint8_t> rgba; uint32_t w = 0, h = 0, levels = 1; SamplerDesc smp; } extra_[3];
  PipelineState st_;
  std::vector<uint8_t> tex_; uint32_t tex_w_ = 0, tex_h_ = 0; SamplerDesc smp_;
  std::vector<uint8_t> ps_cb0_;
  std::vector<uint8_t> cb0_;
  std::vector<uint32_t> vs_, fs_; bool vs_ubo_ = false;
  struct Target { std::vector<uint8_t> last_pixels; pnr::graphics::FrameSurface surf; bool has_surface = false; float clear[4] = {0, 0, 0, 0}; };
  pnr::graphics::VulkanBackend& vk_;
  std::map<ResourceId, Target> targets_;
};
}}
