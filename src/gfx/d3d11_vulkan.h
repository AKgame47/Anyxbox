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
// Current limits (reported as errors, never silently ignored): the viewport must cover the whole render
// target; vertex alpha is not carried (output alpha is 1); no depth, blending or textures.
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
 // Replace the pass-through shaders with translated SPIR-V (see shader/dxbc_spirv.h for the interface rules).
  // The vertex layout stays fixed: location 0 = float4 clip position, location 1 = float3 colour.
  // Empty vectors restore the pass-through pair. The vertex shader must come from dxbc_to_spirv with vulkan_flip_y
  // (the default): with custom shaders the backend no longer negates input y, the shader negates its output y.
  void SetShaders(std::vector<uint32_t> vs, std::vector<uint32_t> fs) { vs_ = std::move(vs); fs_ = std::move(fs); }
 // cb0 contents are pushed to the vertex shader as push constants (max 128 bytes = 8 float4 registers; the Vulkan
 // guaranteed minimum). Larger buffers make the draw fail with an error rather than truncating.
 void set_vs_constants(const std::vector<uint8_t>& cb0) override { cb0_ = cb0; }
 // cb0 for the pixel shader is bound as a Vulkan uniform buffer (set 1, binding 0), up to 16384 bytes.
 void set_ps_constants(const std::vector<uint8_t>& cb0) override { ps_cb0_ = cb0; }
 private:
  std::vector<uint8_t> ps_cb0_;
  std::vector<uint8_t> cb0_;
  std::vector<uint32_t> vs_, fs_;
  struct Target { pnr::graphics::FrameSurface surf; bool has_surface = false; float clear[4] = {0, 0, 0, 0}; };
  pnr::graphics::VulkanBackend& vk_;
  std::map<ResourceId, Target> targets_;
};
}}
