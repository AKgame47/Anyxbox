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
 private:
  struct Target { pnr::graphics::FrameSurface surf; bool has_surface = false; float clear[4] = {0, 0, 0, 0}; };
  pnr::graphics::VulkanBackend& vk_;
  std::map<ResourceId, Target> targets_;
};
}}
