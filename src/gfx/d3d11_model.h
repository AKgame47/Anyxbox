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

struct InputElement { std::string semantic; Format format; uint32_t offset; uint32_t slot = 0; };   // slot: vertex buffer slot 0..15 (per-vertex data only)
struct Viewport { float x = 0, y = 0, width = 0, height = 0; float min_depth = 0, max_depth = 1; };
struct DrawStats { uint64_t triangles_in = 0, triangles_drawn = 0, pixels_written = 0; };

// Fixed-function state (D3D11 semantics). Defaults match D3D11's defaults for a null state object.
enum class Cull { None, Front, Back };
enum class Compare { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class BlendFactor { Zero, One, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DestAlpha, InvDestAlpha, DestColor, InvDestColor };
enum class BlendOp { Add, Subtract, RevSubtract, Min, Max };
struct ScissorRect { int32_t left = 0, top = 0, right = 0, bottom = 0; };
struct RasterizerDesc { Cull cull = Cull::Back; bool front_ccw = false; bool scissor_enable = false; };   // front = clockwise on screen by default
struct DepthStencilDesc { bool depth_enable = true; bool depth_write = true; Compare func = Compare::Less; };
struct BlendDesc { bool enable = false; BlendFactor src = BlendFactor::One, dst = BlendFactor::Zero; BlendOp op = BlendOp::Add;
                   BlendFactor src_a = BlendFactor::One, dst_a = BlendFactor::Zero; BlendOp op_a = BlendOp::Add; };
struct PipelineState { RasterizerDesc rs; DepthStencilDesc ds; BlendDesc blend; bool depth_bound = false; ScissorRect scissor; };

struct ClipVertex { float x, y, z, w; float r, g, b, a; float u = 0, v = 0; };  // D3D clip space (y up)

struct SamplerDesc { bool linear = false; bool wrap = false; bool mip_linear = false; float min_lod = 0, max_lod = 1000; };   // point/linear min-mag and mip filter, clamp/wrap (U and V)
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
  // Fixed-function state for the next draw. Default: ignored (backends that cannot honour it should fail the draw).
  virtual void set_pipeline_state(const PipelineState&) {}
  // Depth buffer cleared (value in 0..1). Return false with *err when the backend cannot do it.
  virtual bool clear_depth(Device&, float, std::string*) { return true; }
  virtual void set_vs_constants(const std::vector<uint8_t>& /*cb0*/) {}
  virtual void set_ps_constants(const std::vector<uint8_t>& /*cb0*/) {}
  // Pixel-shader texture t0 for the next draw, RGBA byte order, row 0 at top; rgba == nullptr = none bound.
  virtual void set_ps_texture(const uint8_t* /*rgba*/, uint32_t /*w*/, uint32_t /*h*/, const SamplerDesc& /*s*/) {}
  virtual void set_ps_mip_chain(uint32_t, const std::vector<uint8_t>&, uint32_t) {}
  // Textures t1..t3 (slot 1..3); a null rgba unbinds. Default: ignored.
  virtual void set_ps_texture_slot(uint32_t /*slot*/, const uint8_t* /*rgba*/, uint32_t /*w*/, uint32_t /*h*/, const SamplerDesc& /*s*/) {}
};

class Device {
 public:
  ResourceId CreateBuffer(const void* data, size_t bytes);           // 0 on failure
  ResourceId CreateTexture2D(uint32_t w, uint32_t h, Format f, uint32_t levels = 1);       // RGBA8 or BGRA8 only; max 16384
  bool UpdateBuffer(ResourceId buffer, const void* data, size_t bytes);   // whole buffer, same size
  bool UpdateTexture(ResourceId texture, const void* data, size_t bytes);   // whole texture, in the texture's own byte order
  bool GenerateMips(ResourceId texture); // recursive area box filter for RGBA8/BGRA8
  bool UpdateTextureMip(ResourceId texture, uint32_t level, const void* data, size_t bytes);
  ResourceId CreateDepthBuffer(uint32_t w, uint32_t h);               // 32-bit float depth, 0 on failure
  ResourceId CreateDepthStencilView(ResourceId depth_buffer);          // 0 on failure
  struct Depth { uint32_t w = 0, h = 0; std::vector<float> z; };
  Depth* depth(ResourceId id) { return id && id <= depths_.size() ? &depths_[id - 1] : nullptr; }
  ResourceId dsv_depth(ResourceId dsv) const { return dsv && dsv <= dsvs_.size() ? dsvs_[dsv - 1] : 0; }
  ResourceId CreateRenderTargetView(ResourceId texture);              // 0 on failure
  bool ReadTexture(ResourceId texture, std::vector<uint8_t>* out) const;  // row 0 = top
  // internal, used by DeviceContext
  struct Buffer { std::vector<uint8_t> data; };
  struct Texture { uint32_t w = 0, h = 0; Format f = Format::Unknown; std::vector<uint8_t> px; std::vector<std::vector<uint8_t>> mips; bool auto_mips = false; };
  const Buffer* buffer(ResourceId id) const;
  Texture* texture(ResourceId id);
  const Texture* texture(ResourceId id) const;
  ResourceId rtv_texture(ResourceId rtv) const;
 private:
  std::vector<Buffer> buffers_;     // id = index + 1
  std::vector<Texture> textures_;
  std::vector<Depth> depths_;
  std::vector<ResourceId> dsvs_;
  std::vector<ResourceId> rtvs_;    // rtv id -> texture id
  // ids are in separate spaces by kind; callers must not mix them
};

class DeviceContext {
 public:
  // The model's own default is no culling (older callers rely on it); D3D11CreateDevice-style users get D3D's default
  // (cull back, clockwise front) from the COM layer, which calls RSSetState(RasterizerDesc()) at creation.
  explicit DeviceContext(Device& d) : dev_(d) { state_.rs.cull = Cull::None; }
  void SetDrawBackend(DrawBackend* b) { backend_ = b; }  // null = built-in reference renderer
  void IASetInputLayout(const std::vector<InputElement>& layout) { layout_ = layout; }
  void IASetVertexBuffer(ResourceId buffer, uint32_t stride, uint32_t offset) { vbs_[0] = {buffer, stride, offset}; }
  bool IASetVertexBufferSlot(uint32_t slot, ResourceId buffer, uint32_t stride, uint32_t offset) {
    if (slot >= 16) return fail("IASetVertexBuffer: slot must be 0..15");
    vbs_[slot] = {buffer, stride, offset}; return true; }
  void IASetPrimitiveTopology(Topology t) { topo_ = t; }
  void RSSetViewport(const Viewport& v) { vp_ = v; has_vp_ = true; }
  // Only slot 0 exists for now; returns false (and sets last_error) for any other slot or an invalid buffer. 0 unbinds.
  // Constant buffer slots 0..13. With a layout set (from SpirvResult::cb_layout) the buffers bound to the declared slots are
  // packed into one blob, slot after slot, each cut or zero-padded to its declared register count (see shader/dxbc_spirv.h);
  // an unbound slot reads as zeros. With no layout (empty) the backend gets slot 0's bytes as they are.
  void SetConstantLayout(std::vector<std::pair<uint32_t, uint32_t>> vs, std::vector<std::pair<uint32_t, uint32_t>> ps) { vs_layout_ = std::move(vs); ps_layout_ = std::move(ps); }
  bool VSSetConstantBuffer(uint32_t slot, ResourceId buffer);
  bool PSSetConstantBuffer(uint32_t slot, ResourceId buffer);
  bool PSSetShaderResource(uint32_t slot, ResourceId texture);   // slots 0..3; 0 unbinds
  bool PSSetSampler(uint32_t slot, const SamplerDesc& s);        // slots 0..3
  // Which sampler slot each texture slot is sampled with (SpirvResult::tex_sampler; -1 or never called = same number).
  void SetTexturePairing(const int pair[4]) { for (int i = 0; i < 4; i++) tex_pair_[i] = pair[i]; }
  void OMSetRenderTarget(ResourceId rtv, ResourceId dsv = 0) { rtv_ = rtv; dsv_ = dsv; }
  void RSSetScissorRect(const ScissorRect& r) { state_.scissor = r; }
  void RSSetState(const RasterizerDesc& d) { state_.rs = d; }
  void OMSetDepthStencilState(const DepthStencilDesc& d) { state_.ds = d; }
  void OMSetBlendState(const BlendDesc& d) { state_.blend = d; }
  bool ClearDepthStencilView(ResourceId dsv, float depth);
  bool ClearRenderTargetView(ResourceId rtv, const float rgba[4]);
  bool Draw(uint32_t vertex_count, uint32_t start_vertex);
  // Index buffer: 16 or 32 bit unsigned indices; byte offset into the buffer. Slot-0 vertex buffer only, like Draw.
  bool IASetIndexBuffer(ResourceId buffer, bool index32, uint32_t offset);
  bool DrawIndexed(uint32_t index_count, uint32_t start_index, int32_t base_vertex);
  const std::string& last_error() const { return err_; }
  const DrawStats& stats() const { return stats_; }
 private:
  bool fail(const char* m) { err_ = m; return false; }
  Device& dev_;
  DrawBackend* backend_ = nullptr;
  std::vector<InputElement> layout_;
  bool draw_impl(uint32_t count, uint32_t start, bool indexed, int32_t base_vertex);
  ResourceId ib_ = 0; bool ib32_ = false; uint32_t ib_offset_ = 0;
  PipelineState state_;
  ResourceId dsv_ = 0;
  struct VBind { ResourceId buf = 0; uint32_t stride = 0, offset = 0; } vbs_[16];
  ResourceId rtv_ = 0, ps_texs_[4] = {};
  int tex_pair_[4] = {-1, -1, -1, -1};
  ResourceId vs_cbs_[14] = {}, ps_cbs_[14] = {};
  std::vector<std::pair<uint32_t, uint32_t>> vs_layout_, ps_layout_;
  SamplerDesc ps_smps_[4];
  Topology topo_ = Topology::Undefined;
  Viewport vp_; bool has_vp_ = false;
  std::string err_;
  DrawStats stats_;
};
}}
