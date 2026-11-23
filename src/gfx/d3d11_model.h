// Anyxbox Phase 3 start: a Direct3D 11 style resource and command model, plus a CPU reference
// renderer used to pixel-verify it. GPL-2.0.
//
// The model (Device, DeviceContext, resources, state, Draw) is the part a Vulkan backend will
// implement. The reference renderer is a test oracle in the same role as Microsoft's WARP: exact
// integer rasterization with the D3D top-left fill rule. It is NOT how games will be drawn.
//
// Scope of this slice: triangle lists, one vertex buffer slot, POSITION/COLOR inputs with a
// built-in pass-through pipeline (real DXBC/DXIL shaders come later), one render target, no depth,
// no blending, no clipping (triangles with w <= 0 are dropped).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace anyxbox { namespace d3d11 {

enum class Format { Unknown, R8G8B8A8_UNORM, B8G8R8A8_UNORM, R32G32_FLOAT, R32G32B32_FLOAT, R32G32B32A32_FLOAT };
enum class Topology { Undefined, TriangleList };
using ResourceId = uint32_t;  // 0 = invalid

struct InputElement { std::string semantic; Format format; uint32_t offset; };
struct Viewport { float x = 0, y = 0, width = 0, height = 0; };
struct DrawStats { uint64_t triangles_in = 0, triangles_drawn = 0, pixels_written = 0; };

struct ClipVertex { float x, y, z, w; float r, g, b, a; };  // D3D clip space (y up)

class Device;
// Pluggable draw path. The reference renderer is built in; the Vulkan backend lives in d3d11_vulkan.h.
class DrawBackend {
 public:
  virtual ~DrawBackend() = default;
  // Called after DeviceContext validated the draw. `tex` is the render target texture id.
  virtual bool clear(Device& dev, ResourceId tex, const float rgba[4], std::string* err) = 0;
  virtual bool draw(Device& dev, ResourceId tex, const Viewport& vp, const std::vector<ClipVertex>& verts,
                    DrawStats* stats, std::string* err) = 0;
  // Vertex-shader constant buffer 0 contents for the next draw (empty = none bound). Default: ignored.
  virtual void set_vs_constants(const std::vector<uint8_t>& /*cb0*/) {}
};

class Device {
 public:
  ResourceId CreateBuffer(const void* data, size_t bytes);           // 0 on failure
  ResourceId CreateTexture2D(uint32_t w, uint32_t h, Format f);       // RGBA8 or BGRA8 only; max 16384
  ResourceId CreateRenderTargetView(ResourceId texture);              // 0 on failure
  bool ReadTexture(ResourceId texture, std::vector<uint8_t>* out) const;  // row 0 = top
  // internal, used by DeviceContext
  struct Buffer { std::vector<uint8_t> data; };
  struct Texture { uint32_t w = 0, h = 0; Format f = Format::Unknown; std::vector<uint8_t> px; };
  const Buffer* buffer(ResourceId id) const;
  Texture* texture(ResourceId id);
  const Texture* texture(ResourceId id) const;
  ResourceId rtv_texture(ResourceId rtv) const;
 private:
  std::vector<Buffer> buffers_;     // id = index + 1
  std::vector<Texture> textures_;
  std::vector<ResourceId> rtvs_;    // rtv id -> texture id
  // ids are in separate spaces by kind; callers must not mix them
};

class DeviceContext {
 public:
  explicit DeviceContext(Device& d) : dev_(d) {}
  void SetDrawBackend(DrawBackend* b) { backend_ = b; }  // null = built-in reference renderer
  void IASetInputLayout(const std::vector<InputElement>& layout) { layout_ = layout; }
  void IASetVertexBuffer(ResourceId buffer, uint32_t stride, uint32_t offset) { vb_ = buffer; stride_ = stride; vb_offset_ = offset; }
  void IASetPrimitiveTopology(Topology t) { topo_ = t; }
  void RSSetViewport(const Viewport& v) { vp_ = v; has_vp_ = true; }
  // Only slot 0 exists for now; returns false (and sets last_error) for any other slot or an invalid buffer. 0 unbinds.
  bool VSSetConstantBuffer(uint32_t slot, ResourceId buffer);
  void OMSetRenderTarget(ResourceId rtv) { rtv_ = rtv; }
  bool ClearRenderTargetView(ResourceId rtv, const float rgba[4]);
  bool Draw(uint32_t vertex_count, uint32_t start_vertex);
  const std::string& last_error() const { return err_; }
  const DrawStats& stats() const { return stats_; }
 private:
  bool fail(const char* m) { err_ = m; return false; }
  Device& dev_;
  DrawBackend* backend_ = nullptr;
  std::vector<InputElement> layout_;
  ResourceId vb_ = 0, rtv_ = 0, vs_cb0_ = 0;
  uint32_t stride_ = 0, vb_offset_ = 0;
  Topology topo_ = Topology::Undefined;
  Viewport vp_; bool has_vp_ = false;
  std::string err_;
  DrawStats stats_;
};
}}
