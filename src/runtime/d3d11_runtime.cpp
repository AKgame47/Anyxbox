#include "runtime/d3d11_runtime.h"
#include <algorithm>
#include <cstring>
#include <cmath>
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
enum Kind : uint32_t { K_DEVICE, K_CONTEXT, K_BUFFER, K_TEXTURE, K_RTV, K_VS, K_PS, K_LAYOUT, K_DEPTH, K_DSV, K_RS, K_DSS, K_BLEND, K_SWAPCHAIN, K_SRV, K_SAMPLER, K_FACTORY };
struct Obj {
  void** vtbl;            // must be first: guest code reads it
  uint32_t refs = 1;
  Kind kind;
  D3D11Runtime* rt;
  uint32_t usage = 0, bind_flags = 0;
  uint32_t descriptor[11] = {}; // public buffer(6) or texture2D(11) UINT fields
  uint32_t first_mip = 0, mip_count = ~0u; // SRV range
  ResourceId id = 0;      // buffer / texture / rtv id
  std::vector<uint32_t> spirv;
  std::vector<std::pair<uint32_t, uint32_t>> cb_layout; bool vs_ubo = false; int tex_sampler[4] = {-1, -1, -1, -1};   // shaders only
  std::vector<InputElement> layout;
  RasterizerDesc rs; DepthStencilDesc ds; BlendDesc bl;
  Obj* resource = nullptr; // view-owned reference to originating COM resource
  Obj* backbuffer = nullptr;   // swap chain only
  SamplerDesc smp;
  std::vector<uint8_t> shadow; bool mapped = false; uint32_t mapped_sub = 0;   // Map/Unmap staging for buffers and textures
};
D3D11Runtime* G(void* self) { return ((Obj*)self)->rt; }
MSABI int32_t stub_notimpl() { return E_NOTIMPL_; }
// Interface identifiers from public mingw-w64 d3d11.h, not executable code.
static const uint8_t iid_DeviceChild[16]={0xc8,0xe5,0x41,0x18,0xb0,0x16,0x9b,0x48,0xbc,0xc8,0x44,0xcf,0xb0,0xd5,0xde,0xae};
static const uint8_t iid_Resource[16]={0xf3,0x63,0x8e,0xdc,0x2b,0xd1,0x52,0x49,0xb4,0x7b,0x5e,0x45,0x02,0x6a,0x86,0x2d};
static const uint8_t iid_Buffer[16]={0x85,0x0b,0x57,0x48,0xee,0xd1,0xcd,0x4f,0xa2,0x50,0xeb,0x35,0x07,0x22,0xb0,0x37};
static const uint8_t iid_Texture2D[16]={0xf2,0xaa,0x15,0x6f,0x08,0xd2,0x89,0x4e,0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c};
static const uint8_t iid_DeviceContext[16]={0x6c,0xa9,0xbf,0xc0,0x89,0xe0,0xfb,0x44,0x8e,0xaf,0x26,0xf8,0x79,0x61,0x90,0xda};
static const uint8_t iid_Device[16]={0xdb,0x6d,0x6f,0xdb,0x77,0xac,0x88,0x4e,0x82,0x53,0x81,0x9d,0xf9,0xbb,0xf1,0x40};
static const uint8_t iid_View[16]={0x16,0x12,0x9d,0x83,0x2e,0xbb,0x2b,0x41,0xb7,0xf4,0xa9,0xdb,0xeb,0xe0,0x8e,0xd1};
static const uint8_t iid_RenderTargetView[16]={0x67,0xa0,0xdb,0xdf,0x8d,0x0b,0x65,0x48,0x87,0x5b,0xd7,0xb4,0x51,0x6c,0xc1,0x64};
static const uint8_t iid_ShaderResourceView[16]={0xe0,0x6f,0xe0,0xb0,0x92,0x81,0x1a,0x4e,0xb1,0xca,0x36,0xd7,0x41,0x47,0x10,0xb2};
static const uint8_t iid_DepthStencilView[16]={0x2a,0xc9,0xda,0x9f,0x76,0x18,0xc3,0x48,0xaf,0xad,0x25,0xb9,0x4f,0x84,0xa9,0xb6};
MSABI int32_t qi(void* self, const uint8_t* iid, void** out) {
  static const uint8_t unk[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
  if (!out) return E_INVALIDARG_;
  auto* o=(Obj*)self;
  auto is=[&](const uint8_t* value){return iid && std::memcmp(iid,value,16)==0;};
  bool resource=o->kind==K_BUFFER || o->kind==K_TEXTURE || o->kind==K_DEPTH;
  bool child=o->kind!=K_DEVICE && o->kind!=K_SWAPCHAIN && o->kind!=K_FACTORY;
  bool supported=is(unk) || (o->kind==K_DEVICE && is(iid_Device)) || (o->kind==K_CONTEXT && is(iid_DeviceContext)) ||
    (child && is(iid_DeviceChild)) || (resource && is(iid_Resource)) || (o->kind==K_BUFFER && is(iid_Buffer)) ||
    ((o->kind==K_TEXTURE || o->kind==K_DEPTH) && is(iid_Texture2D)) ||
    ((o->kind==K_RTV || o->kind==K_SRV || o->kind==K_DSV) && is(iid_View)) ||
    (o->kind==K_RTV && is(iid_RenderTargetView)) || (o->kind==K_SRV && is(iid_ShaderResourceView)) ||
    (o->kind==K_DSV && is(iid_DepthStencilView));
  if(supported){o->refs++;*out=self;return S_OK_;}
  *out = nullptr; return E_NOINTERFACE_;
}
MSABI uint32_t addref(void* self) { return ++((Obj*)self)->refs; }
MSABI uint32_t release(void* self) {
  Obj* o = (Obj*)self;
  uint32_t r = --o->refs;
  if(r==0 && o->kind!=K_DEVICE && o->kind!=K_CONTEXT){
    if(o->resource)release(o->resource);
    if(o->kind==K_SWAPCHAIN && o->backbuffer)release(o->backbuffer);
    delete o;
  } // device and context live in the runtime
  return r;
}
// ---- vtables (filled in Impl) ----
}  // namespace

struct D3D11Runtime::Impl {
  Obj device, context;
  void* dev_vt[43]; void* ctx_vt[116]; void* child_vt[16]; void* resource_vt[16]; void* view_vt[16]; void* sc_vt[20]; void* fac_vt[25];
  // bound state
  Obj* vs = nullptr; Obj* ps = nullptr; Obj* rtv = nullptr; Obj* dsv = nullptr;
  Obj* srvs[4] = {};Obj* vbs[16] = {};Obj* ib = nullptr;
  Obj* vs_cbs[14] = {};Obj* ps_cbs[14] = {};
  Obj* samplers[4] = {};
  Obj* layout=nullptr;Obj* rasterizer=nullptr;Obj* depth_state=nullptr;Obj* blend_state=nullptr;
  uint32_t sample_mask=~0u;
  Impl() {}
  void clear_bindings(){
    sample_mask=~0u;
    auto drop=[](Obj*& o){auto* old=o;o=nullptr;if(old)release(old);};
    for(auto** o:{&vs,&ps,&rtv,&dsv,&ib,&layout,&rasterizer,&depth_state,&blend_state})drop(*o);
    for(auto& o:srvs)drop(o);
    for(auto& o:vbs)drop(o);
    for(auto& o:vs_cbs)drop(o);
    for(auto& o:ps_cbs)drop(o);
    for(auto& o:samplers)drop(o);
  }
  ~Impl(){clear_bindings();}
};

namespace {
Obj* make(D3D11Runtime* rt, Kind k, ResourceId id) {
  Obj* o = new Obj(); o->kind = k; o->rt = rt; o->id = id;
  o->vtbl = (k==K_BUFFER || k==K_TEXTURE || k==K_DEPTH)?rt->impl->resource_vt:(k==K_RTV || k==K_SRV || k==K_DSV)?rt->impl->view_vt:rt->impl->child_vt; return o;
}
MSABI void child_GetDevice(void* self,void** out){
  if(out){auto* d=&G(self)->impl->device;d->refs++;*out=d;}
}
MSABI void resource_GetType(void* self,uint32_t* out){if(out)*out=((Obj*)self)->kind==K_BUFFER?1:3;}
MSABI void resource_GetDesc(void* self,void* out){
  if(out)std::memcpy(out,((Obj*)self)->descriptor,((Obj*)self)->kind==K_BUFFER?24:44);
}
MSABI void view_GetResource(void* self,void** out){
  if(out){auto* r=((Obj*)self)->resource;if(r)++r->refs;*out=r;}
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
  if (out) { auto* o=make(rt,K_BUFFER,id); o->usage=d->Usage; o->bind_flags=d->BindFlags; std::memcpy(o->descriptor,d,sizeof(*d)); *out=o; }
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
  if ((d->Format == 40 || (d->Format == 39 && (d->BindFlags & 0x40))) && (d->BindFlags & 0x40)) {   // D32_FLOAT (or typeless) depth
    if (d->MipLevels > 1 || d->ArraySize > 1 || d->SCount > 1) return E_NOTIMPL_;
    if (init && init->pSysMem) return E_INVALIDARG_;
    ResourceId did = rt->dev.CreateDepthBuffer(d->Width, d->Height);
    if (!did) return E_INVALIDARG_;
    if (out) {
      auto* o=make(rt,K_DEPTH,did);o->usage=d->Usage;o->bind_flags=d->BindFlags;
      std::memcpy(o->descriptor,d,sizeof(*d));o->descriptor[2]=1;o->descriptor[3]=1;o->descriptor[5]=1;*out=o;
    }
    return S_OK_;
  }
  Format f = fmt_from_dxgi(d->Format);
  if (f != Format::R8G8B8A8_UNORM && f != Format::B8G8R8A8_UNORM) return E_INVALIDARG_;
  if (d->ArraySize > 1 || d->SCount > 1) return E_NOTIMPL_;
  ResourceId id = rt->dev.CreateTexture2D(d->Width, d->Height, f, d->MipLevels);
  if (!id) return E_INVALIDARG_;
  rt->dev.texture(id)->auto_mips = (d->BindFlags & 0x28) == 0x28 && (d->Misc & 1);
  if (init) {
    auto* tx = rt->dev.texture(id);
    for (uint32_t l=0;l<=tx->mips.size();++l) {
      uint32_t w = std::max(1u,d->Width>>l), h = std::max(1u,d->Height>>l);
      if (!init[l].pSysMem) return E_INVALIDARG_;
      uint32_t pitch = init[l].pitch ? init[l].pitch : w*4;
      if (pitch < w*4) return E_INVALIDARG_;
      std::vector<uint8_t> px((size_t)w*h*4);
      for(uint32_t y=0;y<h;++y) std::memcpy(px.data()+(size_t)y*w*4,(const uint8_t*)init[l].pSysMem+(size_t)y*pitch,w*4);
      rt->dev.UpdateTextureMip(id,l,px.data(),px.size());
    }
  }
  if (out) { auto* o=make(rt,K_TEXTURE,id); o->usage=d->Usage; o->bind_flags=d->BindFlags; std::memcpy(o->descriptor,d,sizeof(*d)); o->descriptor[2]=(uint32_t)rt->dev.texture(id)->mips.size()+1; o->descriptor[3]=1; o->descriptor[5]=1; *out=o; }
  return S_OK_;
}
MSABI int32_t dev_CreateRTV(void* self, void* res, const uint32_t* desc, void** out) {
  D3D11Runtime* rt = G(self);
  Obj* r = (Obj*)res;
  if (!r || r->kind != K_TEXTURE) return E_INVALIDARG_;
  if (desc && (desc[1] != 4 || desc[2] != 0)) { rt->last_error = "render target view: only texture2D mip zero supported"; return E_NOTIMPL_; }
  ResourceId id = rt->dev.CreateRenderTargetView(r->id);
  if (!id) return E_FAIL_;
  if (out) {auto* o=make(rt,K_RTV,id);o->resource=r;++r->refs;*out=o;}
  return S_OK_;
}
MSABI int32_t dev_CreateSRV(void* self, void* res, const uint32_t* desc, void** out) {
  Obj* r = (Obj*)res;
  if (!r || r->kind != K_TEXTURE) { G(self)->last_error = "shader resource view: only 2D textures are supported"; return E_NOTIMPL_; }
  auto* tx = G(self)->dev.texture(r->id);
  uint32_t total = (uint32_t)tx->mips.size()+1, first=desc ? desc[2] : 0, count=desc ? desc[3] : ~0u;
  if (desc && (desc[1] != 4 || (desc[0] && fmt_from_dxgi(desc[0]) != tx->f))) {
    G(self)->last_error = "shader resource view: format or dimension unsupported"; return E_NOTIMPL_;
  }
  if (first >= total) return E_INVALIDARG_;
  if (count == ~0u) count=total-first;
  if (!count || count > total-first) return E_INVALIDARG_;
  Obj* o = make(G(self), K_SRV, r->id); o->first_mip=first; o->mip_count=count;
  if (out) {o->resource=r;++r->refs;*out=o;} else delete o;
  return S_OK_;
}
// D3D11_SAMPLER_DESC: Filter@0 AddressU@4 AddressV@8 AddressW@12
MSABI int32_t dev_CreateSampler(void* self, const uint32_t* d, void** out) {
  D3D11Runtime* rt = G(self);
  if (!d) return E_INVALIDARG_;
  uint32_t f = d[0];
  bool bad = f >= 0x80 || ((f & 0x14) != 0 && (f & 0x14) != 0x14);   // comparison / anisotropic / mixed min-mag filters
  bool u_ok = d[1] == 1 || d[1] == 3, v_ok = d[2] == 1 || d[2] == 3;   // WRAP or CLAMP; both axes must agree
  if (bad || !u_ok || !v_ok || d[1] != d[2]) { rt->last_error = "sampler state: only point or linear filters with WRAP or CLAMP (same on U and V) are supported"; return E_NOTIMPL_; }
  float bias, mn, mx; std::memcpy(&bias,d+4,4); std::memcpy(&mn,d+11,4); std::memcpy(&mx,d+12,4);
  if (bias != 0) { rt->last_error = "sampler: nonzero mip bias unsupported"; return E_NOTIMPL_; }
  if (!std::isfinite(mn) || !std::isfinite(mx) || mn > mx) return E_INVALIDARG_;
  Obj* o = make(rt, K_SAMPLER, 0); o->smp.mip_linear = (f & 1) != 0; o->smp.min_lod = mn; o->smp.max_lod = mx; o->smp.linear = (f & 0x14) == 0x14; o->smp.wrap = d[1] == 1;
  if (out) *out = o; else delete o;
  return S_OK_;
}
MSABI int32_t dev_CreateDSV(void* self, void* res, const void* /*desc*/, void** out) {
  D3D11Runtime* rt = G(self);
  Obj* r = (Obj*)res;
  if (!r || r->kind != K_DEPTH) return E_INVALIDARG_;
  ResourceId id = rt->dev.CreateDepthStencilView(r->id);
  if (!id) return E_FAIL_;
  if (out) {auto* o=make(rt,K_DSV,id);o->resource=r;++r->refs;*out=o;}
  return S_OK_;
}
bool conv_factor(uint32_t f, BlendFactor* o) {
  switch (f) { case 1: *o = BlendFactor::Zero; return true; case 2: *o = BlendFactor::One; return true;
    case 3: *o = BlendFactor::SrcColor; return true; case 4: *o = BlendFactor::InvSrcColor; return true;
    case 5: *o = BlendFactor::SrcAlpha; return true; case 6: *o = BlendFactor::InvSrcAlpha; return true;
    case 7: *o = BlendFactor::DestAlpha; return true; case 8: *o = BlendFactor::InvDestAlpha; return true;
    case 9: *o = BlendFactor::DestColor; return true; case 10: *o = BlendFactor::InvDestColor; return true; }
  return false;   // SRC_ALPHA_SAT, BLEND_FACTOR, dual source: not supported
}
bool conv_op(uint32_t f, BlendOp* o) {
  switch (f) { case 1: *o = BlendOp::Add; return true; case 2: *o = BlendOp::Subtract; return true; case 3: *o = BlendOp::RevSubtract; return true;
    case 4: *o = BlendOp::Min; return true; case 5: *o = BlendOp::Max; return true; }
  return false;
}
bool conv_cmp(uint32_t f, Compare* o) {
  if (f < 1 || f > 8) return false;
  static const Compare m[8] = {Compare::Never, Compare::Less, Compare::Equal, Compare::LessEqual, Compare::Greater, Compare::NotEqual, Compare::GreaterEqual, Compare::Always};
  *o = m[f - 1]; return true;
}
// D3D11_BLEND_DESC: AlphaToCoverage@0, Independent@4, RenderTarget[0] @8: Enable@0 Src@4 Dst@8 Op@12 SrcA@16 DstA@20 OpA@24 Mask(u8)@28
MSABI int32_t dev_CreateBlendState(void* self, const uint8_t* d, void** out) {
  D3D11Runtime* rt = G(self);
  if (!d) return E_INVALIDARG_;
  uint32_t u[8]; std::memcpy(u, d + 8, 28); uint8_t mask = d[8 + 28];
  uint32_t a2c; std::memcpy(&a2c, d, 4);
  if (a2c || mask != 0x0F) { rt->last_error = "blend state: alpha-to-coverage and partial write masks are not supported"; return E_NOTIMPL_; }
  BlendDesc b; b.enable = u[0] != 0;
  if (b.enable && !(conv_factor(u[1], &b.src) && conv_factor(u[2], &b.dst) && conv_op(u[3], &b.op) &&
                    conv_factor(u[4], &b.src_a) && conv_factor(u[5], &b.dst_a) && conv_op(u[6], &b.op_a))) {
    rt->last_error = "blend state: unsupported blend factor or operation"; return E_NOTIMPL_;
  }
  Obj* o = make(rt, K_BLEND, 0); o->bl = b; if (out) *out = o; else delete o;
  return S_OK_;
}
// D3D11_DEPTH_STENCIL_DESC: DepthEnable@0 WriteMask@4 Func@8 StencilEnable@12
MSABI int32_t dev_CreateDepthStencilState(void* self, const uint32_t* d, void** out) {
  D3D11Runtime* rt = G(self);
  if (!d) return E_INVALIDARG_;
  if (d[3]) { rt->last_error = "depth-stencil state: stencil is not supported"; return E_NOTIMPL_; }
  DepthStencilDesc ds; ds.depth_enable = d[0] != 0; ds.depth_write = d[1] != 0;
  if (!conv_cmp(d[2], &ds.func)) return E_INVALIDARG_;
  Obj* o = make(rt, K_DSS, 0); o->ds = ds; if (out) *out = o; else delete o;
  return S_OK_;
}
// D3D11_RASTERIZER_DESC: Fill@0 Cull@4 FrontCCW@8 DepthBias@12 Clamp@16 Slope@20 DepthClip@24 Scissor@28 MS@32 AA@36
MSABI int32_t dev_CreateRasterizerState(void* self, const uint32_t* d, void** out) {
  D3D11Runtime* rt = G(self);
  if (!d) return E_INVALIDARG_;
  float clamp, slope; std::memcpy(&clamp, &d[4], 4); std::memcpy(&slope, &d[5], 4);
  if (d[0] != 3 || d[3] != 0 || clamp != 0.f || slope != 0.f) {
    rt->last_error = "rasterizer state: only solid fill, no depth bias are supported"; return E_NOTIMPL_;
  }
  if (d[1] < 1 || d[1] > 3) return E_INVALIDARG_;
  RasterizerDesc r; r.cull = d[1] == 1 ? Cull::None : d[1] == 2 ? Cull::Front : Cull::Back; r.front_ccw = d[2] != 0; r.scissor_enable = d[7] != 0;
  Obj* o = make(rt, K_RS, 0); o->rs = r; if (out) *out = o; else delete o;
  return S_OK_;
}
MSABI int32_t dev_CreateInputLayout(void* self, const ElemDesc* e, uint32_t n, const void* /*bytecode*/, size_t /*len*/, void** out) {
  D3D11Runtime* rt = G(self);
  if (!e || n == 0 || n > 32) return E_INVALIDARG_;
  Obj* o = make(rt, K_LAYOUT, 0);
  for (uint32_t i = 0; i < n; i++) {
    Format f = fmt_from_dxgi(e[i].format);
    if (f == Format::Unknown || e[i].slot >= 16 || !e[i].name) { delete o; rt->last_error = "unsupported input element"; return E_INVALIDARG_; }
    o->layout.push_back({e[i].name, f, e[i].offset, e[i].slot});
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
  Obj* o = make(rt, k, 0); o->spirv = std::move(r.words); o->cb_layout = std::move(r.cb_layout); o->vs_ubo = r.vs_ubo; for (int q = 0; q < 4; q++) o->tex_sampler[q] = r.tex_sampler[q];
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
MSABI void ctx_ClearState(void* self){
  auto* rt=G(self);rt->impl->clear_bindings();rt->ctx->ClearState();
}
MSABI void ctx_VSSetShader(void* self, void* shader, void* const*, uint32_t) {
  auto*& bound=G(self)->impl->vs;auto* next=(Obj*)shader;
  if(next)addref(next);
  auto* old=bound;bound=next;if(old)release(old);
}
MSABI void ctx_PSSetShader(void* self, void* shader, void* const*, uint32_t) {
  auto*& bound=G(self)->impl->ps;auto* next=(Obj*)shader;
  if(next)addref(next);
  auto* old=bound;bound=next;if(old)release(old);
}
void get_shader(Obj* bound,void** out,uint32_t* count){
  if(out){if(bound)addref(bound);*out=bound;}
  if(count)*count=0; // dynamic class instances are not implemented
}
MSABI void ctx_VSGetShader(void* self,void** out,void**,uint32_t* count){get_shader(G(self)->impl->vs,out,count);}
MSABI void ctx_PSGetShader(void* self,void** out,void**,uint32_t* count){get_shader(G(self)->impl->ps,out,count);}
void retain_binding(Obj*& bound,Obj* next){
  if(next)addref(next);
  auto* old=bound;bound=next;if(old)release(old);
}
MSABI void ctx_IASetInputLayout(void* self, void* layout) {
  Obj* l = (Obj*)layout;
  retain_binding(G(self)->impl->layout,l);
  G(self)->ctx->IASetInputLayout(l ? l->layout : std::vector<InputElement>());
}
MSABI void ctx_IASetVertexBuffers(void* self, uint32_t start, uint32_t n, void* const* bufs, const uint32_t* strides, const uint32_t* offsets) {
  D3D11Runtime* rt = G(self);
  if(start>16 || n>16-start || (n && (!strides || !offsets))){rt->last_error="vertex buffer slots must lie within 0..15 with strides/offsets";return;}
  Obj* next[16]={};
  for(uint32_t i=0;i<n;++i){next[i]=bufs?(Obj*)bufs[i]:nullptr;if(next[i])addref(next[i]);}
  for(uint32_t i=0;i<n;++i){
    auto* old=rt->impl->vbs[start+i];rt->impl->vbs[start+i]=next[i];
    rt->ctx->IASetVertexBufferSlot(start+i,next[i]?next[i]->id:0,strides[i],offsets[i]);
    if(old)release(old);
  }
}
MSABI void ctx_IASetPrimitiveTopology(void* self, uint32_t t) {
  G(self)->ctx->IASetPrimitiveTopology(t == 4 ? Topology::TriangleList : Topology::Undefined);
}
MSABI void ctx_RSSetViewports(void* self, uint32_t n, const Viewport6* v) {
  auto* rt=G(self);
  if(n>1 || (n && !v)){rt->last_error="viewport: only zero or one viewport is supported";return;}
  if(!n){rt->ctx->RSDisableViewport();return;}
  if(!std::isfinite(v->x)||!std::isfinite(v->y)||!std::isfinite(v->w)||!std::isfinite(v->h)||
     !std::isfinite(v->mn)||!std::isfinite(v->mx)||v->w<0||v->h<0||v->mn<0||v->mx>1||v->mn>v->mx||
     v->x < -32768 || v->y < -32768 || v->x > 32767 || v->y > 32767 ||
     (double)v->x+v->w>32767 || (double)v->y+v->h>32767){rt->last_error="viewport: invalid dimensions or depth bounds";return;}
  rt->ctx->RSSetViewport({v->x,v->y,v->w,v->h,v->mn,v->mx});
}
MSABI void ctx_RSSetScissorRects(void* self, uint32_t n, const ScissorRect* rects) {
  auto rt = G(self);
  if (n > 1 || (n && !rects)) { rt->last_error = "scissor: only zero or one rectangle is supported"; return; }
  if (n && (rects->left > rects->right || rects->top > rects->bottom)) { rt->last_error = "scissor: inverted rectangle"; return; }
  rt->ctx->RSSetScissorRect(n ? *rects : ScissorRect());
}
MSABI void ctx_OMSetRenderTargets(void* self, uint32_t n, void* const* rtvs, void* dsv) {
  auto* rt=G(self);
  if(n>1 || (n && !rtvs)){rt->last_error="OMSetRenderTargets: only zero or one view is supported";return;}
  Obj* r=n?(Obj*)rtvs[0]:nullptr;Obj* d=(Obj*)dsv;
  // Acquire both new references before dropping either old one, including rebinds.
  if(r)addref(r);
  if(d)addref(d);
  auto* oldr=rt->impl->rtv;auto* oldd=rt->impl->dsv;
  rt->impl->rtv=r;rt->impl->dsv=d;
  rt->ctx->OMSetRenderTarget(r?r->id:0,d?d->id:0);
  // Supported RTVs address mip zero; nonzero-first SRVs do not overlap.
  for(uint32_t slot=0;slot<4;++slot){
    auto*& srv=rt->impl->srvs[slot];
    if(r && srv && r->resource==srv->resource && srv->first_mip==0){
      auto* old=srv;srv=nullptr;rt->ctx->PSSetShaderResource(slot,0);release(old);
    }
  }
  if(oldr)release(oldr);
  if(oldd)release(oldd);
}
MSABI void ctx_OMSetBlendState(void* self, void* st, const float* /*factor*/, uint32_t mask) {
  G(self)->impl->sample_mask=mask;
  Obj* o = (Obj*)st;retain_binding(G(self)->impl->blend_state,o); G(self)->ctx->OMSetBlendState(o ? o->bl : BlendDesc());
}
MSABI void ctx_OMSetDepthStencilState(void* self, void* st, uint32_t /*stencil_ref*/) {
  Obj* o = (Obj*)st;retain_binding(G(self)->impl->depth_state,o); G(self)->ctx->OMSetDepthStencilState(o ? o->ds : DepthStencilDesc());
}
MSABI void ctx_RSSetState(void* self, void* st) {
  Obj* o = (Obj*)st;retain_binding(G(self)->impl->rasterizer,o); G(self)->ctx->RSSetState(o ? o->rs : RasterizerDesc());
}
MSABI void ctx_GenerateMips(void* self, void* view) {
  auto* rt = G(self); auto* o = (Obj*)view;
  if (!o || o->kind != K_SRV) { rt->last_error = "GenerateMips: requires a shader resource view"; return; }
  auto* tex = rt->dev.texture(o->id);
  if (tex && tex->auto_mips && !rt->dev.GenerateMips(o->id,o->first_mip,o->mip_count)) rt->last_error = "GenerateMips: invalid texture";
}
MSABI void ctx_PSSetShaderResources(void* self, uint32_t start, uint32_t n, void* const* srvs) {
  auto* rt=G(self);
  if(start>4 || n>4-start){rt->last_error="PSSetShaderResources: slots must lie within t0..t3";return;}
  Obj* next[4]={};
  // Capture and retain the entire batch first. Cross-slot swaps may otherwise free
  // an object before its later incoming slot has acquired a reference.
  for(uint32_t i=0;i<n;++i){
    auto* candidate=srvs?(Obj*)srvs[i]:nullptr;auto* target=rt->impl->rtv;
    bool overlap=candidate && target && candidate->resource==target->resource && candidate->first_mip==0;
    next[i]=overlap?nullptr:candidate;if(next[i])addref(next[i]);
  }
  for(uint32_t i=0;i<n;++i){
    auto* o=next[i];auto* old=rt->impl->srvs[start+i];rt->impl->srvs[start+i]=o;
    rt->ctx->PSSetShaderResource(start+i,o?o->id:0,o?o->first_mip:0,o?o->mip_count:~0u);
    if(old)release(old);
  }
}
MSABI void ctx_PSSetSamplers(void* self, uint32_t start, uint32_t n, void* const* smps) {
  auto* rt=G(self);
  if(start>4 || n>4-start){rt->last_error="PSSetSamplers: slots must lie within s0..s3";return;}
  Obj* next[4]={};
  for(uint32_t i=0;i<n;++i){next[i]=smps?(Obj*)smps[i]:nullptr;if(next[i])addref(next[i]);}
  for(uint32_t i=0;i<n;++i){
    auto* old=rt->impl->samplers[start+i];rt->impl->samplers[start+i]=next[i];
    rt->ctx->PSSetSampler(start+i,next[i]?next[i]->smp:DefaultPixelSampler());
    if(old)release(old);
  }
}
MSABI void ctx_IASetIndexBuffer(void* self, void* buf, uint32_t fmt, uint32_t offset) {
  Obj* b = (Obj*)buf;
  if (b && fmt != 57 && fmt != 42) { G(self)->last_error = "index buffer format must be R16_UINT or R32_UINT"; return; }
  auto* rt=G(self);
  if(!rt->ctx->IASetIndexBuffer(b?b->id:0,fmt==42,offset)){rt->last_error=rt->ctx->last_error();return;}
  if(b)addref(b);
  auto* old=rt->impl->ib;rt->impl->ib=b;if(old)release(old);
}
MSABI void ctx_DrawIndexed(void* self, uint32_t count, uint32_t start, int32_t base) {
  D3D11Runtime* rt = G(self);
  Obj* vs = rt->impl->vs; Obj* ps = rt->impl->ps;
  if(!vs || !ps){rt->last_error="draw: this runtime requires bound vertex and pixel shaders";return;}
  if(!(rt->impl->sample_mask&1u))return; // supported targets are single-sample
  if (vs && ps) {
    rt->ctx->SetConstantLayout(vs->cb_layout, ps->cb_layout); rt->ctx->SetTexturePairing(ps->tex_sampler);
    if (rt->on_shaders_ubo) rt->on_shaders_ubo(vs->spirv, ps->spirv, vs->vs_ubo);
    else if (rt->on_shaders) rt->on_shaders(vs->spirv, ps->spirv);
  }
  if (!rt->ctx->DrawIndexed(count, start, base)) rt->last_error = rt->ctx->last_error();
}
// Raw bytes of a buffer or texture resource, for Map / UpdateSubresource.
std::vector<uint8_t>* res_bytes(D3D11Runtime* rt, Obj* o, uint32_t* row_pitch, uint32_t* height, uint32_t sub = 0) {
  if (!o) return nullptr;
  if (o->kind == K_BUFFER) { if (sub) return nullptr; auto* b = rt->dev.buffer(o->id); if (row_pitch) *row_pitch = 0; if (height) *height = 0; return b ? const_cast<std::vector<uint8_t>*>(&b->data) : nullptr; }
  if (o->kind == K_TEXTURE) { auto* t = rt->dev.texture(o->id); if (!t || sub > t->mips.size()) return nullptr; if (row_pitch) *row_pitch = std::max(1u,t->w>>sub)*4; if (height) *height = std::max(1u,t->h>>sub); return sub ? &t->mips[sub-1] : &t->px; }
  return nullptr;
}
// Copies are synchronous host-backed operations; the Vulkan backend re-seeds modified color surfaces on the next draw.
bool copy_valid(D3D11Runtime* rt, Obj* dst, Obj* src) {
  if (!dst || !src || dst->kind != src->kind || (dst->kind != K_TEXTURE && dst->kind != K_BUFFER)) {
    rt->last_error="copy: matching buffers or color textures required"; return false;
  }
  if (dst->mapped || src->mapped || dst->usage == 1) { rt->last_error="copy: mapped resource or immutable destination"; return false; }
  if (dst->kind == K_TEXTURE) {
    auto* a=rt->dev.texture(dst->id); auto* b=rt->dev.texture(src->id);
    if (!a || !b || a->f != b->f) { rt->last_error="copy: identical color formats required"; return false; }
  }
  return true;
}
MSABI void ctx_CopyResource(void* self, void* dest, void* source) {
  auto* rt=G(self); auto* dst=(Obj*)dest; auto* src=(Obj*)source;
  if (!copy_valid(rt,dst,src)) return;
  if (dst->id==src->id) { rt->last_error="CopyResource: distinct resources required"; return; }
  if (dst->kind==K_BUFFER) {
    auto* a=res_bytes(rt,dst,nullptr,nullptr); auto* b=res_bytes(rt,src,nullptr,nullptr);
    if (a->size()!=b->size()) { rt->last_error="CopyResource: buffer sizes differ"; return; } *a=*b;
  } else {
    auto* a=rt->dev.texture(dst->id); auto* b=rt->dev.texture(src->id);
    if(a->w!=b->w || a->h!=b->h || a->mips.size()!=b->mips.size()) {rt->last_error="CopyResource: texture dimensions differ";return;}
    a->px=b->px; a->mips=b->mips;
  }
}
MSABI void ctx_CopySubresourceRegion(void* self, void* dest, uint32_t ds, uint32_t x, uint32_t y, uint32_t z, void* source, uint32_t ss, const uint32_t* box) {
  auto* rt=G(self); auto* dst=(Obj*)dest; auto* src=(Obj*)source;
  if (!copy_valid(rt,dst,src)) return;
  if(dst->id==src->id && ds==ss) {rt->last_error="copy region: distinct subresources required";return;}
  uint32_t dp=0,dh=0,sp=0,sh=0;
  auto* a=res_bytes(rt,dst,&dp,&dh,ds); auto* b=res_bytes(rt,src,&sp,&sh,ss);
  if(!a||!b) {rt->last_error="copy region: invalid subresource";return;}
  bool buffer=dst->kind==K_BUFFER;
  uint32_t sw=buffer?(uint32_t)b->size():sp/4, dw=buffer?(uint32_t)a->size():dp/4;
  if(buffer) {sh=dh=1;sp=sw;dp=dw;}
  uint32_t l=box?box[0]:0,t=box?box[1]:0,f=box?box[2]:0,r=box?box[3]:sw,bot=box?box[4]:sh,back=box?box[5]:1;
  if(l>=r || t>=bot || f>=back) return; // documented empty-box no-op
  if(z || f || back!=1 || l>sw || r>sw || bot>sh || x>dw || y>dh || r-l>dw-x || bot-t>dh-y) {
    rt->last_error="copy region: box/offset outside subresource";return;
  }
  size_t unit=buffer?1:4;
  for(uint32_t row=0;row<bot-t;++row) std::memcpy(a->data()+(size_t)(y+row)*dp+(size_t)x*unit,b->data()+(size_t)(t+row)*sp+(size_t)l*unit,(size_t)(r-l)*unit);
}
// UpdateSubresource(res, sub, box, data, rowPitch, depthPitch). Buffers: box is a byte range in left/right. Textures: a rect.
MSABI void ctx_UpdateSubresource(void* self, void* res, uint32_t sub, const uint32_t* box, const void* data, uint32_t rowpitch, uint32_t /*depthpitch*/) {
  D3D11Runtime* rt = G(self); Obj* o = (Obj*)res; uint32_t pitch = 0, h = 0;
  std::vector<uint8_t>* dst = res_bytes(rt, o, &pitch, &h, sub);
  if (!dst || !data) { rt->last_error = "UpdateSubresource: unsupported resource or subresource"; return; }
  if (o->kind == K_BUFFER) {
    uint32_t l = box ? box[0] : 0, r = box ? box[3] : (uint32_t)dst->size();
    if (l > r || r > dst->size()) { rt->last_error = "UpdateSubresource: box outside the buffer"; return; }
    std::memcpy(dst->data() + l, data, r - l);
  } else {
    uint32_t l = box ? box[0] : 0, t = box ? box[1] : 0, r = box ? box[3] : pitch / 4, b = box ? box[4] : h;
    if (l > r || t > b || r > pitch / 4 || b > h) { rt->last_error = "UpdateSubresource: box outside the texture"; return; }
    uint32_t sp = rowpitch ? rowpitch : (r - l) * 4;
    if (sp < (r - l) * 4) { rt->last_error = "UpdateSubresource: row pitch too small"; return; }
    for (uint32_t y = t; y < b; y++) std::memcpy(dst->data() + (size_t)y * pitch + (size_t)l * 4, (const uint8_t*)data + (size_t)(y - t) * sp, (size_t)(r - l) * 4);
  }
}
// Map(res, sub, mapType, flags, D3D11_MAPPED_SUBRESOURCE{pData, RowPitch, DepthPitch}). Staging copy, written back by Unmap.
MSABI int32_t ctx_Map(void* self, void* res, uint32_t sub, uint32_t type, uint32_t /*flags*/, void** mapped) {
  D3D11Runtime* rt = G(self); Obj* o = (Obj*)res; uint32_t pitch = 0, h = 0;
  std::vector<uint8_t>* src = res_bytes(rt, o, &pitch, &h, sub);
  if (!src || !mapped || type < 1 || type > 5) return E_INVALIDARG_;
  if (o->mapped) return E_FAIL_;
  o->shadow = *src;                                   // READ and READ_WRITE see the contents; DISCARD may too (contents are undefined)
  o->mapped = true; o->mapped_sub = sub;
  mapped[0] = o->shadow.data();
  uint32_t pitches[2] = {pitch, 0}; std::memcpy((uint8_t*)mapped + 8, pitches, 8);
  return S_OK_;
}
MSABI void ctx_Unmap(void* self, void* res, uint32_t sub) {
  D3D11Runtime* rt = G(self); Obj* o = (Obj*)res;
  std::vector<uint8_t>* dst = res_bytes(rt, o, nullptr, nullptr, sub);
  if (!dst || !o->mapped || sub != o->mapped_sub) return;
  if (o->shadow.size() == dst->size()) *dst = o->shadow;
  o->mapped = false; o->shadow.clear();
}
MSABI void ctx_ClearDepthStencilView(void* self, void* dsv, uint32_t flags, float depth, uint8_t /*stencil*/) {
  Obj* d = (Obj*)dsv;
  if (d && (flags & 1)) { if (!G(self)->ctx->ClearDepthStencilView(d->id, depth)) G(self)->last_error = G(self)->ctx->last_error(); }
}
void bind_constants(void* self,uint32_t start,uint32_t n,void* const* bufs,bool pixel){
  auto* rt=G(self);
  if(start>14 || n>14-start){rt->last_error="constant buffer slots must lie within 0..13";return;}
  auto* bound=pixel?rt->impl->ps_cbs:rt->impl->vs_cbs;Obj* next[14]={};
  for(uint32_t i=0;i<n;++i){next[i]=bufs?(Obj*)bufs[i]:nullptr;if(next[i])addref(next[i]);}
  for(uint32_t i=0;i<n;++i){
    auto* old=bound[start+i];bound[start+i]=next[i];auto id=next[i]?next[i]->id:0;
    if(pixel)rt->ctx->PSSetConstantBuffer(start+i,id);else rt->ctx->VSSetConstantBuffer(start+i,id);
    if(old)release(old);
  }
}
MSABI void ctx_VSSetConstantBuffers(void* self,uint32_t start,uint32_t n,void* const* bufs){bind_constants(self,start,n,bufs,false);}
MSABI void ctx_PSSetConstantBuffers(void* self,uint32_t start,uint32_t n,void* const* bufs){bind_constants(self,start,n,bufs,true);}

MSABI void ctx_ClearRenderTargetView(void* self, void* rtv, const float* rgba) {
  Obj* r = (Obj*)rtv; if (r && rgba) G(self)->ctx->ClearRenderTargetView(r->id, rgba);
}
MSABI void ctx_Draw(void* self, uint32_t count, uint32_t start) {
  D3D11Runtime* rt = G(self);
  Obj* vs = rt->impl->vs; Obj* ps = rt->impl->ps;
  if(!vs || !ps){rt->last_error="draw: this runtime requires bound vertex and pixel shaders";return;}
  if(!(rt->impl->sample_mask&1u))return; // supported targets are single-sample
  if (vs && ps) {
    rt->ctx->SetConstantLayout(vs->cb_layout, ps->cb_layout);
    if (rt->on_shaders_ubo) rt->on_shaders_ubo(vs->spirv, ps->spirv, vs->vs_ubo);
    else if (rt->on_shaders) rt->on_shaders(vs->spirv, ps->spirv);
  }
  if (!rt->ctx->Draw(count, start)) rt->last_error = rt->ctx->last_error();
}
}  // namespace

namespace {
MSABI int32_t sc_Present(void* self, uint32_t sync, uint32_t flags);
MSABI int32_t sc_GetBuffer(void* self, uint32_t index, const uint8_t* iid, void** out);
}
namespace {
MSABI int32_t fac_EnumAdapters(void*, uint32_t, void** out);
MSABI int32_t fac_MakeWindowAssociation(void*, void*, uint32_t);
MSABI int32_t fac_CreateSwapChain(void*, void*, const uint8_t*, void**);
MSABI int32_t fac_IsCurrent(void*);
}
D3D11Runtime::D3D11Runtime() : impl(new Impl()) {
  ctx.reset(new DeviceContext(dev));
  for(unsigned slot=0;slot<4;++slot)ctx->PSSetSampler(slot,DefaultPixelSampler());
  ctx->RSSetState(RasterizerDesc());   // D3D11 default: cull back, clockwise front
  Impl& I = *impl;
  for (auto& p : I.dev_vt) p = (void*)&stub_notimpl;
  for (auto& p : I.ctx_vt) p = (void*)&stub_notimpl;
  for (auto& p : I.child_vt) p = (void*)&stub_notimpl;
  for (auto& p : I.resource_vt) p=(void*)&stub_notimpl;
  for (auto& p : I.view_vt) p=(void*)&stub_notimpl;
  for (void** vt : {I.dev_vt, I.ctx_vt, I.child_vt,I.resource_vt,I.view_vt}) { vt[0] = (void*)&qi; vt[1] = (void*)&addref; vt[2] = (void*)&release; }
  // slot numbers: ID3D11DeviceVtbl and ID3D11DeviceContextVtbl member order (mingw-w64 d3d11.h, cross-checked
  // against two public vtable listings; see docs/D3D11_VTABLES.md)
  I.child_vt[3]=I.resource_vt[3]=I.view_vt[3]=I.ctx_vt[3]=(void*)&child_GetDevice;
  I.resource_vt[7]=(void*)&resource_GetType;
  I.resource_vt[10]=(void*)&resource_GetDesc;
  I.view_vt[7]=(void*)&view_GetResource;
  I.dev_vt[3] = (void*)&dev_CreateBuffer; I.dev_vt[5] = (void*)&dev_CreateTexture2D; I.dev_vt[9] = (void*)&dev_CreateRTV;
  I.dev_vt[7] = (void*)&dev_CreateSRV; I.dev_vt[23] = (void*)&dev_CreateSampler; I.dev_vt[10] = (void*)&dev_CreateDSV; I.dev_vt[20] = (void*)&dev_CreateBlendState; I.dev_vt[21] = (void*)&dev_CreateDepthStencilState;
  I.dev_vt[22] = (void*)&dev_CreateRasterizerState; I.dev_vt[11] = (void*)&dev_CreateInputLayout; I.dev_vt[12] = (void*)&dev_CreateVS; I.dev_vt[15] = (void*)&dev_CreatePS;
  I.dev_vt[37] = (void*)&dev_GetFeatureLevel; I.dev_vt[40] = (void*)&dev_GetImmediateContext;
  I.ctx_vt[7] = (void*)&ctx_VSSetConstantBuffers; I.ctx_vt[9] = (void*)&ctx_PSSetShader; I.ctx_vt[11] = (void*)&ctx_VSSetShader;
  I.ctx_vt[13] = (void*)&ctx_Draw; I.ctx_vt[16] = (void*)&ctx_PSSetConstantBuffers; I.ctx_vt[17] = (void*)&ctx_IASetInputLayout;
  I.ctx_vt[18] = (void*)&ctx_IASetVertexBuffers; I.ctx_vt[24] = (void*)&ctx_IASetPrimitiveTopology;
  I.ctx_vt[46] = (void*)&ctx_CopySubresourceRegion; I.ctx_vt[47] = (void*)&ctx_CopyResource;
  I.ctx_vt[54] = (void*)&ctx_GenerateMips;
  I.ctx_vt[8] = (void*)&ctx_PSSetShaderResources; I.ctx_vt[10] = (void*)&ctx_PSSetSamplers; I.ctx_vt[12] = (void*)&ctx_DrawIndexed;
  I.ctx_vt[14] = (void*)&ctx_Map; I.ctx_vt[15] = (void*)&ctx_Unmap; I.ctx_vt[19] = (void*)&ctx_IASetIndexBuffer; I.ctx_vt[48] = (void*)&ctx_UpdateSubresource;
  I.ctx_vt[35] = (void*)&ctx_OMSetBlendState; I.ctx_vt[36] = (void*)&ctx_OMSetDepthStencilState; I.ctx_vt[43] = (void*)&ctx_RSSetState;
  I.ctx_vt[76]=(void*)&ctx_VSGetShader;I.ctx_vt[74]=(void*)&ctx_PSGetShader;
  I.ctx_vt[110]=(void*)&ctx_ClearState;
  I.ctx_vt[45] = (void*)&ctx_RSSetScissorRects;
  I.ctx_vt[53] = (void*)&ctx_ClearDepthStencilView; I.ctx_vt[33] = (void*)&ctx_OMSetRenderTargets; I.ctx_vt[44] = (void*)&ctx_RSSetViewports; I.ctx_vt[50] = (void*)&ctx_ClearRenderTargetView;
  for (auto& p : I.sc_vt) p = (void*)&stub_notimpl;
  I.sc_vt[0] = (void*)&qi; I.sc_vt[1] = (void*)&addref; I.sc_vt[2] = (void*)&release;
  for (auto& p : I.fac_vt) p = (void*)&stub_notimpl;
  I.fac_vt[0] = (void*)&qi; I.fac_vt[1] = (void*)&addref; I.fac_vt[2] = (void*)&release;
  I.fac_vt[7] = (void*)&fac_EnumAdapters; I.fac_vt[8] = (void*)&fac_MakeWindowAssociation; I.fac_vt[10] = (void*)&fac_CreateSwapChain; I.fac_vt[13 - 0] = (void*)&fac_IsCurrent;
  I.sc_vt[8] = (void*)&sc_Present; I.sc_vt[9] = (void*)&sc_GetBuffer;
  I.device.vtbl = I.dev_vt; I.device.kind = K_DEVICE; I.device.rt = this;
  I.context.vtbl = I.ctx_vt; I.context.kind = K_CONTEXT; I.context.rt = this;
}
D3D11Runtime::~D3D11Runtime() {}

namespace {
// IDXGISwapChain: IUnknown 0..2, IDXGIObject 3..6 (GetParent 6), GetDevice 7, Present 8, GetBuffer 9, ...
MSABI int32_t sc_Present(void* self, uint32_t /*sync*/, uint32_t /*flags*/) {
  Obj* sc = (Obj*)self; D3D11Runtime* rt = sc->rt;
  Device::Texture* t = rt->dev.texture(sc->backbuffer->id);
  if (!t) return E_FAIL_;
  rt->presented = t->px;
  if (t->f == Format::B8G8R8A8_UNORM) for (size_t i = 0; i + 3 < rt->presented.size(); i += 4) std::swap(rt->presented[i], rt->presented[i + 2]);
  rt->presented_w = t->w; rt->presented_h = t->h; rt->presents++;
  if (rt->on_present) rt->on_present(rt->presented.data(), t->w, t->h);
  return S_OK_;
}
MSABI int32_t sc_GetBuffer(void* self, uint32_t index, const uint8_t* iid, void** out) {
  Obj* sc = (Obj*)self;
  if (!out) return E_INVALIDARG_;
  if (index != 0) { *out = nullptr; return E_INVALIDARG_; }
  return qi(sc->backbuffer,iid,out);
}
D3D11Runtime* g_rt = nullptr;   // one runtime per guest run; set by register_imports
// DXGI_SWAP_CHAIN_DESC Width@0 Height@4 Format@16 SampleCount@28 BufferCount@40 (72 bytes); shared by CreateSwapChain and D3D11CreateDeviceAndSwapChain
int32_t create_swapchain(const uint8_t* desc, void** swap) {
  *swap = nullptr;
  if (!desc) return E_INVALIDARG_;
  uint32_t w, h, fmt, samples; std::memcpy(&w, desc, 4); std::memcpy(&h, desc + 4, 4); std::memcpy(&fmt, desc + 16, 4); std::memcpy(&samples, desc + 28, 4);
  if (w == 0) w = 640;
  if (h == 0) h = 480;   // 0 means "size of the window", which this headless runtime does not have
  Format f = fmt == 0 ? Format::R8G8B8A8_UNORM : fmt_from_dxgi(fmt);
  if ((f != Format::R8G8B8A8_UNORM && f != Format::B8G8R8A8_UNORM) || samples > 1) return E_INVALIDARG_;
  ResourceId tid = g_rt->dev.CreateTexture2D(w, h, f);
  if (!tid) return E_INVALIDARG_;
  Obj* sc = make(g_rt, K_SWAPCHAIN, 0); sc->vtbl = (void**)g_rt->impl->sc_vt;
  sc->backbuffer = make(g_rt, K_TEXTURE, tid);
  uint32_t usage;std::memcpy(&usage,desc+36,4);
  auto* back=sc->backbuffer;back->bind_flags=((usage&0x20)?0x20u:0u)|((usage&0x10)?8u:0u);
  const uint32_t realized[]={w,h,1,1,fmt?fmt:28u,1,0,0,back->bind_flags,0,0};
  std::memcpy(back->descriptor,realized,sizeof(realized));
  *swap = sc;
  return S_OK_;
}
// IDXGIFactory (slots from https://reverseengineering.wiki/index.php/Fact:DXGI/enum/IDXGIFactory_vtable_slots):
// MakeWindowAssociation 8, CreateSwapChain 10, EnumAdapters 7 (no adapters are exposed), IsCurrent 13 (IDXGIFactory1).
const int32_t DXGI_ERROR_NOT_FOUND_ = (int32_t)0x887A0002;
MSABI int32_t fac_EnumAdapters(void*, uint32_t, void** out) { if (out) *out = nullptr; return DXGI_ERROR_NOT_FOUND_; }
MSABI int32_t fac_MakeWindowAssociation(void*, void*, uint32_t) { return S_OK_; }
MSABI int32_t fac_CreateSwapChain(void* /*self*/, void* /*device*/, const uint8_t* desc, void** out) {
  if (!out || !g_rt) return E_INVALIDARG_;
  return create_swapchain(desc, out);
}
MSABI int32_t fac_IsCurrent(void*) { return 1; }
// CreateDXGIFactory / CreateDXGIFactory1(REFIID, void**): any requested interface id gets the factory (IUnknown/IDXGIFactory/IDXGIFactory1 share one vtable).
MSABI int32_t host_CreateDXGIFactory(const uint8_t* /*iid*/, void** out) {
  if (!out) return E_INVALIDARG_;
  *out = nullptr;
  if (!g_rt) return E_FAIL_;
  Obj* f = make(g_rt, K_FACTORY, 0); f->vtbl = (void**)g_rt->impl->fac_vt;
  *out = f; return S_OK_;
}
MSABI int32_t host_D3D11CreateDevice(void* /*adapter*/, uint32_t /*driver*/, void* /*software*/, uint32_t /*flags*/,
                                     const uint32_t* /*levels*/, uint32_t /*n*/, uint32_t /*sdk*/, void** dev,
                                     uint32_t* level, void** ctx) {
  if (!g_rt) return E_FAIL_;
  if (dev) { g_rt->impl->device.refs++; *dev = &g_rt->impl->device; }
  if (level) *level = 0xb000;
  if (ctx) { g_rt->impl->context.refs++; *ctx = &g_rt->impl->context; }
  return S_OK_;
}
// D3D11CreateDeviceAndSwapChain: DXGI_SWAP_CHAIN_DESC Width@0 Height@4 Format@16 SampleCount@28 BufferCount@40 (72 bytes)
MSABI int32_t host_D3D11CreateDeviceAndSwapChain(void* adapter, uint32_t driver, void* software, uint32_t flags, const uint32_t* levels,
                                                 uint32_t nlevels, uint32_t sdk, const uint8_t* desc, void** swap, void** dev,
                                                 uint32_t* level, void** ctx) {
  if (!g_rt) return E_FAIL_;
  if (swap) { int32_t hr = create_swapchain(desc, swap); if (hr != S_OK_) return hr; }
  int32_t hr = host_D3D11CreateDevice(adapter, driver, software, flags, levels, nlevels, sdk, dev, level, ctx);
  return hr;
}
}  // namespace
void D3D11Runtime::register_imports(ImportRegistry& reg) {
  g_rt = this;
  reg.add("dxgi.dll", "CreateDXGIFactory", (uint64_t)(uintptr_t)&host_CreateDXGIFactory);
  reg.add("dxgi.dll", "CreateDXGIFactory1", (uint64_t)(uintptr_t)&host_CreateDXGIFactory);
  reg.add("d3d11.dll", "D3D11CreateDevice", (uint64_t)(uintptr_t)&host_D3D11CreateDevice);
  reg.add("d3d11.dll", "D3D11CreateDeviceAndSwapChain", (uint64_t)(uintptr_t)&host_D3D11CreateDeviceAndSwapChain);
}
}  // namespace anyxbox
