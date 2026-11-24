// A hand-assembled x64 PE calls d3d11.dll!D3D11CreateDevice, creates a buffer, texture, render target, input
// layout and a real fxc-compiled vertex and pixel shader (tests/real_dxbc_wine.h, see its provenance note),
// binds them through COM vtables and calls Draw. The host then reads the render target.
// Honest framing: the PE is assembled by this test (no real game), vtable slot numbers come from the public
// d3d11 headers (docs/D3D11_VTABLES.md). Linux x86-64 only. GPU part skips when Vulkan is missing.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include "pe_builder.h"
#include "real_dxbc_wine.h"
#include "runtime/d3d11_runtime.h"
#include "runtime/run_pe.h"
#include "gfx/d3d11_vulkan.h"
using namespace anyxbox;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)

namespace {
const uint32_t D = 0x2000;   // .data RVA
const uint32_t kCode = 0x1400;  // code RVA (pe_builder's own code area is too small for this program)
// data slots
enum { DEV = 0, CTX = 8, BUF = 16, TEX = 24, RTV = 32, LAY = 40, VS = 48, PS = 56, LEVEL = 64,
       BUFDESC = 0x100, SUBDATA = 0x120, TEXDESC = 0x140, VP = 0x180, CLR = 0x1A0, STRIDE = 0x1C0, OFFSET = 0x1C8,
       ELEMS = 0x1E0, NAMES = 0x300, VERTS = 0x400, VSB = 0x600, PSB = 0xA00 };
struct V { float x, y, z, w, r, g, b, a; };
const int W = 128, H = 128;

struct Asm {
  std::vector<uint8_t> c;
  uint32_t rva() const { return kCode + (uint32_t)c.size(); }
  void b(std::initializer_list<int> v) { for (int x : v) c.push_back((uint8_t)x); }
  void d32(uint32_t v) { for (int i = 0; i < 4; i++) c.push_back((uint8_t)(v >> (8 * i))); }
  // rip-relative op: prefix bytes, then disp32 to target rva; `tail` = bytes after the disp
  void rip(std::initializer_list<int> op, uint32_t target, int tail = 0) {
    b(op); d32(target - (rva() + 4 + tail));
  }
  // lea reg,[rip+data]: reg 0=rax 1=rcx 2=rdx 8=r8 9=r9
  void lea(int reg, uint32_t off) { rip({reg >= 8 ? 0x4C : 0x48, 0x8D, 0x05 | ((reg & 7) << 3)}, D + off); }
  void load(int reg, uint32_t off) { rip({reg >= 8 ? 0x4C : 0x48, 0x8B, 0x05 | ((reg & 7) << 3)}, D + off); }
  void store_rax(uint32_t off) { rip({0x48, 0x89, 0x05}, D + off); }
  void store_stack_rax(int disp) { b({0x48, 0x89, 0x44, 0x24, disp}); }
  void zero_stack(int disp) { b({0x48, 0xC7, 0x44, 0x24, disp, 0, 0, 0, 0}); }
  void mov_edx(uint32_t v) { b({0xBA}); d32(v); }
  void mov_r8d(uint32_t v) { b({0x41, 0xB8}); d32(v); }
  void zero_r8() { b({0x4D, 0x31, 0xC0}); }
  void zero_r9() { b({0x45, 0x31, 0xC9}); }
  // after a call: if hr < 0 jump to the fail stub (patched later)
  std::vector<size_t> fails;
  void check() { b({0x85, 0xC0, 0x0F, 0x88}); fails.push_back(c.size()); d32(0); }
  void vcall(int idx) { b({0x48, 0x8B, 0x01, 0xFF, 0x90}); d32(idx * 8); }   // mov rax,[rcx]; call [rax+idx*8]
  void on(uint32_t slot) { load(1, slot); }                                   // rcx = object
  void ptr_to_data(uint32_t off, uint32_t slotoff) { lea(0, off); store_rax(slotoff); }
};

std::vector<uint8_t> build_guest() {
  Asm a;
  a.b({0x48, 0x83, 0xEC, 0x68});   // sub rsp,0x68
  // data fix-ups that need absolute addresses (the image base is only known at run time)
  a.ptr_to_data(VERTS, SUBDATA);                 // SubData.pSysMem
  a.ptr_to_data(NAMES, ELEMS + 0);               // elem0 name "POSITION"
  a.ptr_to_data(NAMES + 9, ELEMS + 32);          // elem1 name "COLOR"
  // D3D11CreateDevice(0, HARDWARE, 0, 0, NULL, 0, 7, &dev, &level, &ctx)
  a.b({0x31, 0xC9}); a.mov_edx(1); a.zero_r8(); a.zero_r9();
  a.zero_stack(0x20); a.zero_stack(0x28); a.b({0x48, 0xC7, 0x44, 0x24, 0x30, 7, 0, 0, 0});
  a.lea(0, DEV); a.store_stack_rax(0x38); a.lea(0, LEVEL); a.store_stack_rax(0x40); a.lea(0, CTX); a.store_stack_rax(0x48);
  a.rip({0xFF, 0x15}, 0x1260); a.check();
  // dev->CreateBuffer(&bufdesc, &subdata, &buf)          slot 3
  a.on(DEV); a.lea(2, BUFDESC); a.lea(8, SUBDATA); a.lea(9, BUF); a.vcall(3); a.check();
  // dev->CreateTexture2D(&texdesc, NULL, &tex)           slot 5
  a.on(DEV); a.lea(2, TEXDESC); a.zero_r8(); a.lea(9, TEX); a.vcall(5); a.check();
  // dev->CreateRenderTargetView(tex, NULL, &rtv)         slot 9
  a.on(DEV); a.load(2, TEX); a.zero_r8(); a.lea(9, RTV); a.vcall(9); a.check();
  // dev->CreateInputLayout(elems, 2, vsblob, len, &layout)  slot 11
  a.on(DEV); a.lea(2, ELEMS); a.mov_r8d(2); a.lea(9, VSB);
  a.b({0x48, 0xC7, 0x44, 0x24, 0x20}); a.d32((uint32_t)sizeof wine_vs_code);
  a.lea(0, LAY); a.store_stack_rax(0x28); a.vcall(11); a.check();
  // dev->CreateVertexShader(vsblob, len, NULL, &vs)      slot 12
  a.on(DEV); a.lea(2, VSB); a.mov_r8d((uint32_t)sizeof wine_vs_code); a.zero_r9(); a.lea(0, VS); a.store_stack_rax(0x20); a.vcall(12); a.check();
  // dev->CreatePixelShader(psblob, len, NULL, &ps)       slot 15
  a.on(DEV); a.lea(2, PSB); a.mov_r8d((uint32_t)sizeof wine_ps_code); a.zero_r9(); a.lea(0, PS); a.store_stack_rax(0x20); a.vcall(15); a.check();
  // ctx->OMSetRenderTargets(1, &rtv, NULL)               slot 33
  a.on(CTX); a.mov_edx(1); a.lea(8, RTV); a.zero_r9(); a.vcall(33);
  // ctx->RSSetViewports(1, &vp)                          slot 44
  a.on(CTX); a.mov_edx(1); a.lea(8, VP); a.vcall(44);
  // ctx->ClearRenderTargetView(rtv, color)               slot 50
  a.on(CTX); a.load(2, RTV); a.lea(8, CLR); a.vcall(50);
  // ctx->IASetInputLayout(layout)                        slot 17
  a.on(CTX); a.load(2, LAY); a.vcall(17);
  // ctx->IASetVertexBuffers(0, 1, &buf, &stride, &offset) slot 18
  a.on(CTX); a.b({0x31, 0xD2}); a.mov_r8d(1); a.lea(9, BUF);
  a.lea(0, STRIDE); a.store_stack_rax(0x20); a.lea(0, OFFSET); a.store_stack_rax(0x28); a.vcall(18);
  // ctx->IASetPrimitiveTopology(TRIANGLELIST = 4)        slot 24
  a.on(CTX); a.mov_edx(4); a.vcall(24);
  // ctx->VSSetShader(vs, NULL, 0)                        slot 11 ; PSSetShader slot 9
  a.on(CTX); a.load(2, VS); a.zero_r8(); a.zero_r9(); a.vcall(11);
  a.on(CTX); a.load(2, PS); a.zero_r8(); a.zero_r9(); a.vcall(9);
  // Caller releases its references after binding. Draw must use context-owned shaders.
  a.on(VS);a.vcall(2);a.on(PS);a.vcall(2);
  // Release caller target/resource references. Context must hold the view and its resource.
  a.on(RTV);a.vcall(2);a.on(TEX);a.vcall(2);
  // ctx->Draw(3, 0)                                      slot 13
  a.on(CTX); a.mov_edx(3); a.zero_r8(); a.vcall(13);
  a.b({0x31, 0xC0});               // eax = 0
  a.b({0x48, 0x83, 0xC4, 0x68, 0xC3});
  size_t fail_at = a.c.size();
  a.b({0x48, 0x83, 0xC4, 0x68, 0xC3});   // fail stub: eax already holds the failing HRESULT
  for (size_t p : a.fails) { uint32_t rel = (uint32_t)(fail_at - (p + 4)); std::memcpy(&a.c[p], &rel, 4); }
  if (a.c.size() > 0xC00) std::printf("guest code too large\n");
  auto pe = pebuild::make2({0xC3}, {"D3D11CreateDevice"}, "d3d11.dll");
  // .text raw grows to 0x1000 bytes (file 0x200..0x1200); code lives at RVA 0x1400 (file 0x600); entry points there.
  size_t o = 0x84 + 20, s1 = o + 240;
  pebuild::w32(pe, o + 16, kCode);
  pebuild::w32(pe, s1 + 16, 0x1000);
  pe.resize(0x1200, 0);
  std::memcpy(&pe[0x600], a.c.data(), a.c.size());
  return pe;
}
void put32(std::vector<uint8_t>& d, size_t o, uint32_t v) { std::memcpy(&d[o], &v, 4); }
std::vector<uint8_t> finish_pe(std::vector<uint8_t> pe, const std::vector<V>& verts) {
  std::vector<uint8_t> data(0x1000, 0);
  put32(data, BUFDESC, (uint32_t)(verts.size() * sizeof(V))); put32(data, BUFDESC + 8, 1);   // ByteWidth, BindFlags=VERTEX_BUFFER
  const uint32_t td[11] = {W, H, 1, 1, 28, 1, 0, 0, 0x20 /*RENDER_TARGET*/, 0, 0};
  std::memcpy(&data[TEXDESC], td, sizeof td);
  const float vp[6] = {0, 0, (float)W, (float)H, 0, 1}; std::memcpy(&data[VP], vp, sizeof vp);
  const float clr[4] = {0.02f, 0.03f, 0.2f, 1}; std::memcpy(&data[CLR], clr, sizeof clr);
  put32(data, STRIDE, sizeof(V));
  // elem descs: {name*@0, index@8, format@12, slot@16, offset@20, class@24, step@28}; 2 = R32G32B32A32_FLOAT
  put32(data, ELEMS + 12, 2); put32(data, ELEMS + 20, 0);
  put32(data, ELEMS + 32 + 12, 2); put32(data, ELEMS + 32 + 20, 16);
  std::memcpy(&data[NAMES], "POSITION", 9); std::memcpy(&data[NAMES + 9], "COLOR", 6);
  std::memcpy(&data[VERTS], verts.data(), verts.size() * sizeof(V));
  std::memcpy(&data[VSB], wine_vs_code, sizeof wine_vs_code);
  std::memcpy(&data[PSB], wine_ps_code, sizeof wine_ps_code);
  // .data section: raw size 0x1000 at file offset 0x600
  size_t s2 = 0x84 + 20 + 240 + 40;
  pebuild::w32(pe, s2 + 16, 0x1000); pebuild::w32(pe, s2 + 20, 0x1200);
  pe.resize(0x1200 + 0x1000, 0);
  std::memcpy(&pe[0x1200], data.data(), data.size());
  return pe;
}
}  // namespace

// Verify missing stages cannot reuse a backend shader pair from the previous draw.
bool refuses_missing_shaders(D3D11Runtime& rt){
  using Create=int32_t(__attribute__((ms_abi)) *)(void*,uint32_t,void*,uint32_t,void*,uint32_t,uint32_t,void**,void*,void**);
  using Get=void(__attribute__((ms_abi)) *)(void*,void**,void**,uint32_t*);
  using Bind=void(__attribute__((ms_abi)) *)(void*,void*,void*,uint32_t);
  using Draw=void(__attribute__((ms_abi)) *)(void*,uint32_t,uint32_t);
  using Indexed=void(__attribute__((ms_abi)) *)(void*,uint32_t,uint32_t,int32_t);
  using Release=uint32_t(__attribute__((ms_abi)) *)(void*);
  ImportRegistry reg;rt.register_imports(reg);void* d=nullptr;void* c=nullptr;
  auto create=(Create)(uintptr_t)reg.resolve("d3d11.dll","D3D11CreateDevice",0,false);
  if(create(nullptr,1,nullptr,0,nullptr,0,7,&d,nullptr,&c)!=0)return false;
  auto vt=*(void***)c;void* shaders[2]={};((Get)vt[76])(c,&shaders[0],nullptr,nullptr);((Get)vt[74])(c,&shaders[1],nullptr,nullptr);
  auto beforeStats=rt.ctx->stats();std::vector<uint8_t> before,after;if(!rt.dev.ReadTexture(1,&before))return false;
  for(unsigned stage:{11u,9u}){
    ((Bind)vt[stage])(c,nullptr,nullptr,0);
    ((Draw)vt[13])(c,3,0);if(rt.last_error!="draw: this runtime requires bound vertex and pixel shaders")return false;
    rt.last_error.clear();((Indexed)vt[12])(c,3,0,0);
    if(rt.last_error!="draw: this runtime requires bound vertex and pixel shaders")return false;
    rt.last_error.clear();((Bind)vt[stage])(c,shaders[stage==11?0:1],nullptr,0);
  }
  if(!rt.dev.ReadTexture(1,&after)||before!=after)return false;
  if(rt.ctx->stats().triangles_in!=beforeStats.triangles_in||rt.ctx->stats().pixels_written!=beforeStats.pixels_written)return false;
  for(auto* shader:shaders)((Release)(*(void***)shader)[2])(shader);
  ((Release)vt[2])(c);((Release)(*(void***)d)[2])(d);
  return true;
}

bool viewport_binding_checks(D3D11Runtime& rt){
  using Create=int32_t(__attribute__((ms_abi)) *)(void*,uint32_t,void*,uint32_t,void*,uint32_t,uint32_t,void**,void*,void**);
  using Set=void(__attribute__((ms_abi)) *)(void*,uint32_t,const float*);
  using Draw=void(__attribute__((ms_abi)) *)(void*,uint32_t,uint32_t);
  using Release=uint32_t(__attribute__((ms_abi)) *)(void*);
  ImportRegistry reg;rt.register_imports(reg);void* d=nullptr;void* c=nullptr;
  if(((Create)(uintptr_t)reg.resolve("d3d11.dll","D3D11CreateDevice",0,false))(nullptr,1,nullptr,0,nullptr,0,7,&d,nullptr,&c)!=0)return false;
  auto vt=*(void***)c;auto set=(Set)vt[44];auto draw=(Draw)vt[13];
  float valid[]={0,0,128,128,0,1};auto stats=rt.ctx->stats();
  std::vector<uint8_t> before,after;if(!rt.dev.ReadTexture(1,&before))return false;
  set(c,0,nullptr);draw(c,3,0);
  if(rt.last_error!="draw: viewport not set")return false;
  rt.last_error.clear();if(!rt.dev.ReadTexture(1,&after)||after!=before||rt.ctx->stats().triangles_in!=stats.triangles_in)return false;
  set(c,1,valid);
  set(c,2,valid);if(rt.last_error!="viewport: only zero or one viewport is supported")return false;rt.last_error.clear();
  set(c,1,nullptr);if(rt.last_error.empty())return false;rt.last_error.clear();
  float invalid[][6]={{0,0,-1,128,0,1},{0,0,128,128,1,0},{32767,0,1,128,0,1},{0,0,128,128,0,2}};
  for(auto& v:invalid){set(c,1,v);if(rt.last_error!="viewport: invalid dimensions or depth bounds")return false;rt.last_error.clear();}
  draw(c,3,0);if(!rt.last_error.empty()||rt.ctx->stats().triangles_in!=stats.triangles_in+1)return false;
  ((Release)vt[2])(c);((Release)(*(void***)d)[2])(d);return true;
}

int main() {
#if !(defined(__linux__) && defined(__x86_64__))
  std::printf("SKIP: guest code runs on Linux x86-64 only\n"); return 0;
#else
  static_assert(sizeof wine_vs_code < 0x400 && sizeof wine_ps_code < 0x400, "blob slots");
  std::vector<V> t = {{-0.8f,-0.8f,0,1, 1,0,0,1}, {0,0.8f,0,1, 0,0,1,1}, {0.8f,-0.8f,0,1, 0,1,0,1}}   /* clockwise: survives D3D's default back-face culling */;
  auto pe = finish_pe(build_guest(), t);

  // 1. No GPU: the guest runs, creates everything, translates both real shaders, and draws on the reference renderer.
  {
    D3D11Runtime rt;
    auto r = run_pe(pe.data(), pe.size(), true, [&](ImportRegistry& reg) { rt.register_imports(reg); });
    if (!r.loaded || !r.ran) std::printf("run failed: %s\n", r.error.c_str());
    CHECK(r.loaded && r.ran);
    std::printf("guest exit code 0x%x, last_error='%s'\n", r.exit_code, rt.last_error.c_str());
    CHECK(r.exit_code == 0);
    CHECK(rt.shaders_translated == 2);
    CHECK(rt.last_error.empty());
    CHECK(rt.ctx->stats().triangles_drawn == 1);
    d3d11::ResourceId tex = 1;
    std::vector<uint8_t> px; CHECK(rt.dev.ReadTexture(tex, &px));
    auto at = [&](int x, int y) { return &px[((size_t)y * W + x) * 4]; };
    CHECK(at(2, 2)[2] > 40 && at(2, 2)[0] < 20);              // cleared navy at the corner
    CHECK(at(64, 80)[0] + at(64, 80)[1] + at(64, 80)[2] > 200);   // triangle centre is lit
    CHECK(refuses_missing_shaders(rt));
    CHECK(viewport_binding_checks(rt));
    std::puts("viewport binding: zero disables, invalid batches preserve prior viewport (reference)");
    std::puts("missing shader stages: Draw/DrawIndexed refused without pixel or stat changes (reference)");
    std::printf("guest PE drew through the COM vtables (reference renderer): ok\n");
  }
  // 2. Vulkan: same PE, shaders really executed (software GPU), compared with the reference renderer.
  pnr::graphics::VulkanBackend vk;
  auto s = vk.open();
  if (!s) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  s = vk.create_instance("anyxbox-test");
  if (!s || vk.devices().empty()) { std::printf("SKIP gpu part: no device\n"); return 0; }
  if (!(s = vk.create_device(0))) { std::printf("SKIP gpu part: %s\n", s.error.message.c_str()); return 0; }
  D3D11Runtime rt, ref;
  d3d11::VulkanDrawBackend be(vk);
  rt.backend = &be; rt.ctx->SetDrawBackend(&be);
  rt.on_shaders = [&](const std::vector<uint32_t>& v, const std::vector<uint32_t>& p) { be.SetShaders(v, p); };
  auto r1 = run_pe(pe.data(), pe.size(), true, [&](ImportRegistry& reg) { rt.register_imports(reg); });
  auto r2 = run_pe(pe.data(), pe.size(), true, [&](ImportRegistry& reg) { ref.register_imports(reg); });
  CHECK(r1.ran && r2.ran && r1.exit_code == 0 && r2.exit_code == 0);
  std::vector<uint8_t> a, b; CHECK(rt.dev.ReadTexture(1, &a) && ref.dev.ReadTexture(1, &b));
  int bad = 0, cov = 0;
  for (size_t i = 0; i + 3 < a.size(); i += 4) { int d = 0; for (int k = 0; k < 3; k++) d = std::max(d, std::abs((int)a[i + k] - (int)b[i + k])); if (d > 3) bad++; if (b[i] > 60 || b[i + 1] > 60) cov++; }
  std::printf("guest PE via Vulkan vs reference: covered %d, mismatches %d, err='%s'\n", cov, bad, rt.last_error.c_str());
  CHECK(bad == 0 && cov > 3000 && rt.last_error.empty());
  CHECK(refuses_missing_shaders(rt));
  CHECK(viewport_binding_checks(rt));
  std::puts("viewport binding: zero disables, invalid batches preserve prior viewport (Vulkan)");
  std::puts("missing shader stages: Draw/DrawIndexed refused without stale shader pixels or stat changes (Vulkan)");
  std::printf("d3d11 guest: all ok\n");
  return 0;
#endif
}
