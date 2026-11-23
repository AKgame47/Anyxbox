#include "runtime/d3d11_runtime.h"
#include <cstring>
#include "shader/dxbc_spirv.h"
#if defined(__x86_64__)
#define MSABI __attribute__((ms_abi))
#else
#define MSABI
#endif
namespace anyxbox {
using namespace d3d11;
namespace {
const int32_t S_OK_ = 0;
const int32_t E_NOTIMPL_ = (int32_t)0x80004001, E_NOINTERFACE_ = (int32_t)0x80004002, E_FAIL_ = (int32_t)0x80004005,
              E_INVALIDARG_ = (int32_t)0x80070057;
enum Kind : uint32_t { K_DEVICE, K_CONTEXT, K_BUFFER, K_TEXTURE, K_RTV, K_VS, K_PS, K_LAYOUT };
struct Obj {
  void** vtbl;            // must be first: guest code reads it
  uint32_t refs = 1;
  Kind kind;
  D3D11Runtime* rt;
  ResourceId id = 0;      // buffer / texture / rtv id
  std::vector<uint32_t> spirv;
  std::vector<InputElement> layout;
};
D3D11Runtime* G(void* self) { return ((Obj*)self)->rt; }
MSABI int32_t stub_notimpl() { return E_NOTIMPL_; }
MSABI int32_t qi(void* self, const uint8_t* iid, void** out) {
  static const uint8_t unk[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
  if (!out) return E_INVALIDARG_;
  if (iid && std::memcmp(iid, unk, 16) == 0) { ((Obj*)self)->refs++; *out = self; return S_OK_; }
  *out = nullptr; return E_NOINTERFACE_;
}
MSABI uint32_t addref(void* self) { return ++((Obj*)self)->refs; }
MSABI uint32_t release(void* self) {
  Obj* o = (Obj*)self;
  uint32_t r = --o->refs;
  if (r == 0 && o->kind != K_DEVICE && o->kind != K_CONTEXT) delete o;   // device and context live in the runtime
  return r;
}
// ---- vtables (filled in Impl) ----
}  // namespace

struct D3D11Runtime::Impl {
  Obj device, context;
  void* dev_vt[43]; void* ctx_vt[116]; void* child_vt[16];
  // bound state
  Obj* vs = nullptr; Obj* ps = nullptr;
  Impl() {}
};

namespace {
Obj* make(D3D11Runtime* rt, Kind k, ResourceId id) {
  Obj* o = new Obj(); o->kind = k; o->rt = rt; o->id = id;
  o->vtbl = (void**)rt->impl->child_vt; return o;
}
// ---- ID3D11Device ----
struct BufDesc { uint32_t ByteWidth, Usage, BindFlags, CPUAccessFlags, MiscFlags, StructureByteStride; };
struct SubData { const void* pSysMem; uint32_t pitch, slice; };
struct TexDesc { uint32_t Width, Height, MipLevels, ArraySize, Format, SCount, SQuality, Usage, BindFlags, CPUAccess, Misc; };
struct ElemDesc { const char* name; uint32_t index, format, slot, offset, cls, step; };
MSABI int32_t dev_CreateBuffer(void* self, const BufDesc* d, const SubData* init, void** out) {
  D3D11Runtime* rt = G(self);
  if (!d || d->ByteWidth == 0 || d->ByteWidth > (64u << 20)) return E_INVALIDARG_;
  std::vector<uint8_t> z;
  const void* src = init && init->pSysMem ? init->pSysMem : (z.assign(d->ByteWidth, 0), z.data());
  ResourceId id = rt->dev.CreateBuffer(src, d->ByteWidth);
  if (!id) return E_FAIL_;
  if (out) *out = make(rt, K_BUFFER, id);
  return S_OK_;
}
Format fmt_from_dxgi(uint32_t f) {
  switch (f) { case 28: return Format::R8G8B8A8_UNORM; case 87: return Format::B8G8R8A8_UNORM;
    case 2: return Format::R32G32B32A32_FLOAT; case 6: return Format::R32G32B32_FLOAT; case 16: return Format::R32G32_FLOAT; }
  return Format::Unknown;
}
MSABI int32_t dev_CreateTexture2D(void* self, const TexDesc* d, const SubData* init, void** out) {
  D3D11Runtime* rt = G(self);
  if (!d) return E_INVALIDARG_;
  Format f = fmt_from_dxgi(d->Format);
  if (f != Format::R8G8B8A8_UNORM && f != Format::B8G8R8A8_UNORM) return E_INVALIDARG_;
  if (d->MipLevels > 1 || d->ArraySize > 1 || d->SCount > 1) return E_NOTIMPL_;
  ResourceId id = rt->dev.CreateTexture2D(d->Width, d->Height, f);
  if (!id) return E_INVALIDARG_;
  if (init && init->pSysMem) {
    // The model takes a tightly packed image; repack when the guest's row pitch differs.
    std::vector<uint8_t> px((size_t)d->Width * d->Height * 4);
    uint32_t pitch = init->pitch ? init->pitch : d->Width * 4;
    if (pitch < d->Width * 4) return E_INVALIDARG_;
    for (uint32_t y = 0; y < d->Height; y++)
      std::memcpy(&px[(size_t)y * d->Width * 4], (const uint8_t*)init->pSysMem + (size_t)y * pitch, (size_t)d->Width * 4);
    rt->dev.UpdateTexture(id, px.data(), px.size());
  }
  if (out) *out = make(rt, K_TEXTURE, id);
  return S_OK_;
}
MSABI int32_t dev_CreateRTV(void* self, void* res, const void* /*desc*/, void** out) {
  D3D11Runtime* rt = G(self);
  Obj* r = (Obj*)res;
  if (!r || r->kind != K_TEXTURE) return E_INVALIDARG_;
  ResourceId id = rt->dev.CreateRenderTargetView(r->id);
  if (!id) return E_FAIL_;
  if (out) *out = make(rt, K_RTV, id);
  return S_OK_;
}
MSABI int32_t dev_CreateInputLayout(void* self, const ElemDesc* e, uint32_t n, const void* /*bytecode*/, size_t /*len*/, void** out) {
  D3D11Runtime* rt = G(self);
  if (!e || n == 0 || n > 32) return E_INVALIDARG_;
  Obj* o = make(rt, K_LAYOUT, 0);
  for (uint32_t i = 0; i < n; i++) {
    Format f = fmt_from_dxgi(e[i].format);
    if (f == Format::Unknown || e[i].slot != 0 || !e[i].name) { delete o; rt->last_error = "unsupported input element"; return E_INVALIDARG_; }
    o->layout.push_back({e[i].name, f, e[i].offset});
  }
  if (out) *out = o; else delete o;
  return S_OK_;
}
int32_t create_shader(void* self, const void* code, size_t len, void** out, Kind k) {
  D3D11Runtime* rt = G(self);
  if (!code || len < 32) return E_INVALIDARG_;
  shader::SpirvResult r = shader::dxbc_to_spirv((const uint8_t*)code, len);
  if (!r.ok || r.is_pixel != (k == K_PS)) {
    rt->last_error = r.ok ? "shader stage does not match the Create call" : r.error;
    return E_INVALIDARG_;
  }
  rt->shaders_translated++;
  Obj* o = make(rt, k, 0); o->spirv = std::move(r.words);
  if (out) *out = o; else delete o;
  return S_OK_;
}
MSABI int32_t dev_CreateVS(void* self, const void* c, size_t n, void* /*linkage*/, void** out) { return create_shader(self, c, n, out, K_VS); }
MSABI int32_t dev_CreatePS(void* self, const void* c, size_t n, void* /*linkage*/, void** out) { return create_shader(self, c, n, out, K_PS); }
MSABI uint32_t dev_GetFeatureLevel(void*) { return 0xb000; }
MSABI void dev_GetImmediateContext(void* self, void** out) {
  if (!out) return;
  Obj* c = &G(self)->impl->context; c->refs++; *out = c;
}
// ---- ID3D11DeviceContext ----
struct Viewport6 { float x, y, w, h, mn, mx; };
MSABI void ctx_VSSetShader(void* self, void* shader, void* const*, uint32_t) {
  G(self)->impl->vs = (Obj*)shader;
}
MSABI void ctx_PSSetShader(void* self, void* shader, void* const*, uint32_t) {
  G(self)->impl->ps = (Obj*)shader;
}
MSABI void ctx_IASetInputLayout(void* self, void* layout) {
  Obj* l = (Obj*)layout;
  G(self)->ctx->IASetInputLayout(l ? l->layout : std::vector<InputElement>());
}
MSABI void ctx_IASetVertexBuffers(void* self, uint32_t start, uint32_t n, void* const* bufs, const uint32_t* strides, const uint32_t* offsets) {
  D3D11Runtime* rt = G(self);
  if (start != 0 || n != 1 || !bufs || !strides || !offsets) { rt->last_error = "only vertex buffer slot 0 is supported"; return; }
  Obj* b = (Obj*)bufs[0];
  rt->ctx->IASetVertexBuffer(b ? b->id : 0, strides[0], offsets[0]);
}
MSABI void ctx_IASetPrimitiveTopology(void* self, uint32_t t) {
  G(self)->ctx->IASetPrimitiveTopology(t == 4 ? Topology::TriangleList : Topology::Undefined);
}
MSABI void ctx_RSSetViewports(void* self, uint32_t n, const Viewport6* v) {
  if (n >= 1 && v) G(self)->ctx->RSSetViewport({v->x, v->y, v->w, v->h});
}
MSABI void ctx_OMSetRenderTargets(void* self, uint32_t n, void* const* rtvs, void* /*dsv*/) {
  Obj* r = n >= 1 && rtvs ? (Obj*)rtvs[0] : nullptr;
  G(self)->ctx->OMSetRenderTarget(r ? r->id : 0);
}
MSABI void ctx_VSSetConstantBuffers(void* self, uint32_t start, uint32_t n, void* const* bufs) {
  for (uint32_t i = 0; i < n; i++) { Obj* b = bufs ? (Obj*)bufs[i] : nullptr; G(self)->ctx->VSSetConstantBuffer(start + i, b ? b->id : 0); }
}
MSABI void ctx_PSSetConstantBuffers(void* self, uint32_t start, uint32_t n, void* const* bufs) {
  for (uint32_t i = 0; i < n; i++) { Obj* b = bufs ? (Obj*)bufs[i] : nullptr; G(self)->ctx->PSSetConstantBuffer(start + i, b ? b->id : 0); }
}
MSABI void ctx_ClearRenderTargetView(void* self, void* rtv, const float* rgba) {
  Obj* r = (Obj*)rtv; if (r && rgba) G(self)->ctx->ClearRenderTargetView(r->id, rgba);
}
MSABI void ctx_Draw(void* self, uint32_t count, uint32_t start) {
  D3D11Runtime* rt = G(self);
  Obj* vs = rt->impl->vs; Obj* ps = rt->impl->ps;
  if (vs && ps && rt->on_shaders) rt->on_shaders(vs->spirv, ps->spirv);
  if (!rt->ctx->Draw(count, start)) rt->last_error = rt->ctx->last_error();
}
}  // namespace

D3D11Runtime::D3D11Runtime() : impl(new Impl()) {
  ctx.reset(new DeviceContext(dev));
  Impl& I = *impl;
  for (auto& p : I.dev_vt) p = (void*)&stub_notimpl;
  for (auto& p : I.ctx_vt) p = (void*)&stub_notimpl;
  for (auto& p : I.child_vt) p = (void*)&stub_notimpl;
  for (void** vt : {I.dev_vt, I.ctx_vt, I.child_vt}) { vt[0] = (void*)&qi; vt[1] = (void*)&addref; vt[2] = (void*)&release; }
  // slot numbers: ID3D11DeviceVtbl and ID3D11DeviceContextVtbl member order (mingw-w64 d3d11.h, cross-checked
  // against two public vtable listings; see docs/D3D11_VTABLES.md)
  I.dev_vt[3] = (void*)&dev_CreateBuffer; I.dev_vt[5] = (void*)&dev_CreateTexture2D; I.dev_vt[9] = (void*)&dev_CreateRTV;
  I.dev_vt[11] = (void*)&dev_CreateInputLayout; I.dev_vt[12] = (void*)&dev_CreateVS; I.dev_vt[15] = (void*)&dev_CreatePS;
  I.dev_vt[37] = (void*)&dev_GetFeatureLevel; I.dev_vt[40] = (void*)&dev_GetImmediateContext;
  I.ctx_vt[7] = (void*)&ctx_VSSetConstantBuffers; I.ctx_vt[9] = (void*)&ctx_PSSetShader; I.ctx_vt[11] = (void*)&ctx_VSSetShader;
  I.ctx_vt[13] = (void*)&ctx_Draw; I.ctx_vt[16] = (void*)&ctx_PSSetConstantBuffers; I.ctx_vt[17] = (void*)&ctx_IASetInputLayout;
  I.ctx_vt[18] = (void*)&ctx_IASetVertexBuffers; I.ctx_vt[24] = (void*)&ctx_IASetPrimitiveTopology;
  I.ctx_vt[33] = (void*)&ctx_OMSetRenderTargets; I.ctx_vt[44] = (void*)&ctx_RSSetViewports; I.ctx_vt[50] = (void*)&ctx_ClearRenderTargetView;
  I.device.vtbl = I.dev_vt; I.device.kind = K_DEVICE; I.device.rt = this;
  I.context.vtbl = I.ctx_vt; I.context.kind = K_CONTEXT; I.context.rt = this;
}
D3D11Runtime::~D3D11Runtime() {}

namespace {
D3D11Runtime* g_rt = nullptr;   // one runtime per guest run; set by register_imports
MSABI int32_t host_D3D11CreateDevice(void* /*adapter*/, uint32_t /*driver*/, void* /*software*/, uint32_t /*flags*/,
                                     const uint32_t* /*levels*/, uint32_t /*n*/, uint32_t /*sdk*/, void** dev,
                                     uint32_t* level, void** ctx) {
  if (!g_rt) return E_FAIL_;
  if (dev) { g_rt->impl->device.refs++; *dev = &g_rt->impl->device; }
  if (level) *level = 0xb000;
  if (ctx) { g_rt->impl->context.refs++; *ctx = &g_rt->impl->context; }
  return S_OK_;
}
}  // namespace
void D3D11Runtime::register_imports(ImportRegistry& reg) {
  g_rt = this;
  reg.add("d3d11.dll", "D3D11CreateDevice", (uint64_t)(uintptr_t)&host_D3D11CreateDevice);
}
}  // namespace anyxbox
