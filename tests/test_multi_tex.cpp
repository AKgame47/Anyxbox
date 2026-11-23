// Anyxbox: several textures in one pixel shader (t0, t2 and t1/t3 with their samplers), hand-built DXBC, through the model + Vulkan backend.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include "dxbc_builder.h"
#include "gfx/d3d11_vulkan.h"
#include "shader/dxbc_spirv.h"
#include "runtime/d3d11_runtime.h"
#include <cstring>
using namespace anyxbox;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
using dxb::ins; using dxb::dst; using dxb::src;
// o0 = sample(tA, sA, uv) + sample(tB, sB, uv)
static std::vector<uint8_t> ps_two(uint32_t ta, uint32_t sa, uint32_t tb, uint32_t sb) {
  std::vector<dxb::Words> code = {ins(98, {dst(dxb::T_INPUT, 1, 0x3)}, false, 2), ins(101, {dst(dxb::T_OUTPUT, 0, 0xF)}),
    dxb::dcl_tex2d(ta), dxb::dcl_tex2d(tb), dxb::dcl_sampler(sa)};
  if (sb != sa) code.push_back(dxb::dcl_sampler(sb));
  code.push_back(dxb::dcl_temps(2));
  code.push_back(ins(69, {dst(dxb::T_TEMP, 0, 0xF), src(dxb::T_INPUT, 1, dxb::swz(0, 1, 0, 0)), dxb::res_src(ta), dxb::smp_src(sa)}));
  code.push_back(ins(69, {dst(dxb::T_TEMP, 1, 0xF), src(dxb::T_INPUT, 1, dxb::swz(0, 1, 0, 0)), dxb::res_src(tb), dxb::smp_src(sb)}));
  code.push_back(ins(dxb::OP_ADD, {dst(dxb::T_OUTPUT, 0, 0xF), src(dxb::T_TEMP, 0), src(dxb::T_TEMP, 1)}));
  code.push_back(ins(dxb::OP_RET, {}));
  return dxb::shader(0, {{"TEXCOORD", 0, 0, 1, 0x3}}, {{"SV_Target", 0, 0, 0, 0xF}}, code);
}
#define MS __attribute__((ms_abi))
template<class F> static F slot(void* o,int n) { return (F)(*(void***)o)[n]; }
int main() {
  { D3D11Runtime rt; ImportRegistry reg; rt.register_imports(reg); void* dev=nullptr; void* ctx=nullptr; uint32_t fl=0;
    auto create=(int32_t (MS *)(void*,uint32_t,void*,uint32_t,const uint32_t*,uint32_t,uint32_t,void**,uint32_t*,void**))reg.resolve("d3d11.dll","D3D11CreateDevice",0,false);
    CHECK(create(nullptr,1,nullptr,0,nullptr,0,7,&dev,&fl,&ctx)==0);
    uint32_t desc[11]={4,4,0,1,28,1,0,0,8,0,0}; void* tex=nullptr;
    uint8_t base[80]={},mip1[24]={},mip2[4]={}; base[0]=11; mip1[0]=22; mip2[0]=33;
    struct Init {const void* p;uint32_t pitch,slice;}; Init init[3]={{base,20,0},{mip1,12,0},{mip2,4,0}};
    CHECK((slot<int32_t (MS *)(void*,const uint32_t*,const Init*,void**)>(dev,5)(dev,desc,init,&tex)==0));
    auto map=slot<int32_t (MS *)(void*,void*,uint32_t,uint32_t,uint32_t,void*)>(ctx,14);
    auto unmap=slot<void (MS *)(void*,void*,uint32_t)>(ctx,15);
    struct Mapped {void* p;uint32_t pitch,slice;}; Mapped mapped{};
    for(uint32_t l=0;l<3;++l) {CHECK(map(ctx,tex,l,1,0,&mapped)==0);CHECK(*(uint8_t*)mapped.p==11*(l+1) && mapped.pitch==(4u>>l)*4);unmap(ctx,tex,l);}
    CHECK(map(ctx,tex,3,1,0,&mapped)<0);
    uint8_t replacement[4]={44,55,66,255}; slot<void (MS *)(void*,void*,uint32_t,const uint32_t*,const void*,uint32_t,uint32_t)>(ctx,48)(ctx,tex,2,nullptr,replacement,4,0);
    CHECK(map(ctx,tex,2,1,0,&mapped)==0);CHECK(*(uint8_t*)mapped.p==44);unmap(ctx,tex,2);
    void* srv=nullptr; uint32_t view[4]={28,4,0,~0u}; auto srvfn=slot<int32_t (MS *)(void*,void*,const uint32_t*,void**)>(dev,7);
    CHECK(srvfn(dev,tex,view,&srv)==0);
    auto generate=slot<void (MS *)(void*,void*)>(ctx,54);
    generate(ctx,srv); // flags absent: must leave the existing mip contents alone
    CHECK(map(ctx,tex,2,1,0,&mapped)==0); CHECK(*(uint8_t*)mapped.p==44); unmap(ctx,tex,2);
    slot<uint32_t (MS *)(void*)>(srv,2)(srv);
    view[2]=1; CHECK(srvfn(dev,tex,view,&srv)==0); slot<uint32_t (MS *)(void*)>(srv,2)(srv);
    view[2]=3; CHECK(srvfn(dev,tex,view,&srv)<0); view[2]=0; view[3]=0; CHECK(srvfn(dev,tex,view,&srv)<0); view[3]=~0u; rt.last_error.clear();
    slot<uint32_t (MS *)(void*)>(tex,2)(tex);
    desc[8]=0x28; desc[10]=1; CHECK((slot<int32_t (MS *)(void*,const uint32_t*,const Init*,void**)>(dev,5)(dev,desc,nullptr,&tex)==0));
    view[2]=0; CHECK(srvfn(dev,tex,view,&srv)==0);
    uint8_t solid[64]; std::memset(solid,120,sizeof solid);
    auto update=slot<void (MS *)(void*,void*,uint32_t,const uint32_t*,const void*,uint32_t,uint32_t)>(ctx,48);
    update(ctx,tex,0,nullptr,solid,16,0); generate(ctx,srv);
    CHECK(map(ctx,tex,2,1,0,&mapped)==0); CHECK(*(uint8_t*)mapped.p==120); unmap(ctx,tex,2);
    std::memset(solid,80,sizeof solid); update(ctx,tex,0,nullptr,solid,16,0); generate(ctx,srv);
    CHECK(map(ctx,tex,1,1,0,&mapped)==0); CHECK(*(uint8_t*)mapped.p==80); unmap(ctx,tex,1);
    slot<uint32_t (MS *)(void*)>(srv,2)(srv); slot<uint32_t (MS *)(void*)>(tex,2)(tex);
    // A restricted GenerateMips view starts at level 1 and stops at level 2, preserving level 0.
    desc[0]=8; desc[1]=8; CHECK((slot<int32_t (MS *)(void*,const uint32_t*,const Init*,void**)>(dev,5)(dev,desc,nullptr,&tex)==0));
    view[2]=1; view[3]=2; CHECK(srvfn(dev,tex,view,&srv)==0);
    uint8_t base8[256],level1[64],last[4]={77,77,77,77}; std::memset(base8,10,sizeof base8); std::memset(level1,150,sizeof level1);
    update(ctx,tex,0,nullptr,base8,32,0); update(ctx,tex,1,nullptr,level1,16,0); update(ctx,tex,3,nullptr,last,4,0);
    generate(ctx,srv);
    CHECK(map(ctx,tex,0,1,0,&mapped)==0); CHECK(*(uint8_t*)mapped.p==10); unmap(ctx,tex,0);
    CHECK(map(ctx,tex,2,1,0,&mapped)==0); CHECK(*(uint8_t*)mapped.p==150); unmap(ctx,tex,2);
    CHECK(map(ctx,tex,3,1,0,&mapped)==0); CHECK(*(uint8_t*)mapped.p==77); unmap(ctx,tex,3);
    slot<uint32_t (MS *)(void*)>(srv,2)(srv); slot<uint32_t (MS *)(void*)>(tex,2)(tex);
    std::puts("COM subset GenerateMips preserves levels outside the view");
    auto copy=slot<void (MS *)(void*,void*,void*)>(ctx,47);
    auto region=slot<void (MS *)(void*,void*,uint32_t,uint32_t,uint32_t,uint32_t,void*,uint32_t,const uint32_t*)>(ctx,46);
    void* source=nullptr;void* dest=nullptr;desc[8]=0x28;desc[10]=0;desc[0]=4;desc[1]=4;
    auto texture=slot<int32_t (MS *)(void*,const uint32_t*,const Init*,void**)>(dev,5);
    CHECK(texture(dev,desc,nullptr,&source)==0 && texture(dev,desc,nullptr,&dest)==0);
    uint8_t pattern[64]; for(int i=0;i<64;++i) pattern[i]=(uint8_t)i;
    update(ctx,source,0,nullptr,pattern,16,0);update(ctx,source,1,nullptr,pattern,8,0);update(ctx,source,2,nullptr,pattern,4,0);
    copy(ctx,dest,source);
    for(uint32_t l=0;l<3;++l) {CHECK(map(ctx,dest,l,1,0,&mapped)==0);CHECK(std::memcmp(mapped.p,pattern,(4u>>l)*(4u>>l)*4)==0);unmap(ctx,dest,l);}
    uint32_t box[6]={1,1,0,3,3,1};region(ctx,dest,0,0,0,0,source,0,box);
    CHECK(map(ctx,dest,0,1,0,&mapped)==0);CHECK(std::memcmp(mapped.p,pattern+20,8)==0 && std::memcmp((uint8_t*)mapped.p+16,pattern+36,8)==0);unmap(ctx,dest,0);
    rt.last_error.clear();region(ctx,dest,0,~0u,0,0,source,0,box);CHECK(!rt.last_error.empty());
    rt.last_error.clear();box[3]=box[0];region(ctx,dest,0,0,0,0,source,0,box);CHECK(rt.last_error.empty());
    CHECK(map(ctx,source,0,1,0,&mapped)==0);copy(ctx,dest,source);CHECK(!rt.last_error.empty());unmap(ctx,source,0);rt.last_error.clear();
    copy(ctx,source,source);CHECK(!rt.last_error.empty());rt.last_error.clear();
    slot<uint32_t (MS *)(void*)>(source,2)(source);slot<uint32_t (MS *)(void*)>(dest,2)(dest);
    uint32_t bd[6]={16,0,1,0,0,0};Init bi{pattern,0,0};
    auto buffer=slot<int32_t (MS *)(void*,const uint32_t*,const Init*,void**)>(dev,3);
    CHECK(buffer(dev,bd,&bi,&source)==0 && buffer(dev,bd,nullptr,&dest)==0);copy(ctx,dest,source);
    CHECK(map(ctx,dest,0,1,0,&mapped)==0);CHECK(std::memcmp(mapped.p,pattern,16)==0);unmap(ctx,dest,0);
    uint32_t bb[6]={4,0,0,12,1,1};region(ctx,dest,0,0,0,0,source,0,bb);
    CHECK(map(ctx,dest,0,1,0,&mapped)==0);CHECK(std::memcmp(mapped.p,pattern+4,8)==0);unmap(ctx,dest,0);
    slot<uint32_t (MS *)(void*)>(source,2)(source);slot<uint32_t (MS *)(void*)>(dest,2)(dest);
    std::puts("COM CopyResource/CopySubresourceRegion texture mips and buffers passed; bounds, mapped, same-resource and empty-box checks passed");
    std::puts("COM GenerateMips flag gate and regeneration passed");
    std::puts("COM mip initialization, row pitch, subresource update/map and SRV range checks passed");
  }

  auto vsb = dxb::vs_tex_pass();
  auto vsr = shader::dxbc_to_spirv(vsb.data(), vsb.size());
  CHECK(vsr.ok);
  // translator: pairing and limits
  { auto b = ps_two(0, 0, 2, 1); auto r = shader::dxbc_to_spirv(b.data(), b.size());
    CHECK(r.ok && r.tex_sampler[0] == 0 && r.tex_sampler[1] == -1 && r.tex_sampler[2] == 1 && r.tex_sampler[3] == -1);
    auto bad = ps_two(0, 0, 4, 0); r = shader::dxbc_to_spirv(bad.data(), bad.size()); CHECK(!r.ok);
    auto two = dxb::ps_sample(dxb::swz(0, 1, 2, 3), -1, 1, 0, true); r = shader::dxbc_to_spirv(two.data(), two.size()); CHECK(!r.ok);   // t1 not declared
  }
  pnr::graphics::VulkanBackend vk;
  if (!vk.open() || !vk.create_instance("anyxbox-test") || vk.devices().empty() || !vk.create_device(0)) { std::printf("SKIP gpu part\n"); return 0; }
  struct VT { float x, y, z, w, r, g, b, a, u, v; };
  const std::vector<d3d11::InputElement> lay = {{"POSITION", d3d11::Format::R32G32B32A32_FLOAT, 0}, {"COLOR", d3d11::Format::R32G32B32A32_FLOAT, 16}, {"TEXCOORD", d3d11::Format::R32G32_FLOAT, 32}};
  const int W = 32, H = 32;
  struct Case { uint32_t ta, sa, tb, sb; const char* name; };
  const Case cases[] = {{0, 0, 2, 1, "t0+t2 (s0,s1)"}, {0, 0, 1, 0, "t0+t1 (one sampler)"}, {1, 1, 3, 3, "t1+t3 with t0 also bound"}};
  for (auto& c : cases) {
    auto pb = ps_two(c.ta, c.sa, c.tb, c.sb); auto psr = shader::dxbc_to_spirv(pb.data(), pb.size()); CHECK(psr.ok);
    d3d11::VulkanDrawBackend be(vk); be.SetShaders(vsr.words, psr.words);
    d3d11::Device dev; d3d11::DeviceContext ctx(dev); ctx.SetDrawBackend(&be); ctx.SetTexturePairing(psr.tex_sampler);
    auto rt = dev.CreateTexture2D(W, H, d3d11::Format::R8G8B8A8_UNORM); auto rtv = dev.CreateRenderTargetView(rt);
    // four 2x2 textures of different, small values so the sum never saturates: texture k = (10+40k, 20+30k, 5+..., 255) uniform per quadrant pattern
    d3d11::ResourceId tex[4];
    for (int k = 0; k < 4; k++) {
      uint8_t px[2 * 2 * 4];
      for (int i = 0; i < 4; i++) { px[i * 4] = (uint8_t)(10 + 30 * k + 7 * i); px[i * 4 + 1] = (uint8_t)(5 + 20 * k); px[i * 4 + 2] = (uint8_t)(2 * i); px[i * 4 + 3] = 255; }
      tex[k] = dev.CreateTexture2D(2, 2, d3d11::Format::R8G8B8A8_UNORM); CHECK(dev.UpdateTexture(tex[k], px, sizeof px));
    }
    for (int k = 0; k < (4); k++) { CHECK(ctx.PSSetShaderResource(k, tex[k])); CHECK(ctx.PSSetSampler(k, {false, false})); }
    CHECK(!ctx.PSSetShaderResource(4, tex[0]));
    ctx.IASetInputLayout(lay); ctx.IASetPrimitiveTopology(d3d11::Topology::TriangleList); ctx.RSSetViewport({0, 0, (float)W, (float)H}); ctx.OMSetRenderTarget(rtv);
    VT a{-1, 1, 0, 1, 1, 1, 1, 1, 0, 0}, b{1, 1, 0, 1, 1, 1, 1, 1, 1, 0}, cc{-1, -1, 0, 1, 1, 1, 1, 1, 0, 1}, d{1, -1, 0, 1, 1, 1, 1, 1, 1, 1};
    std::vector<VT> q = {a, b, cc, b, d, cc};
    ctx.IASetVertexBuffer(dev.CreateBuffer(q.data(), q.size() * sizeof(VT)), sizeof(VT), 0);
    const float black[4] = {0, 0, 0, 1}; ctx.ClearRenderTargetView(rtv, black);
    if (!ctx.Draw((uint32_t)q.size(), 0)) { std::printf("%s: draw failed: %s\n", c.name, ctx.last_error().c_str()); return 1; }
    std::vector<uint8_t> img; dev.ReadTexture(rt, &img);
    int bad = 0;
    for (int y = 1; y < H; y += 8) for (int x = 1; x < W; x += 8) {
      int i = (y / 16) * 2 + (x / 16);   // texel of the 2x2 textures at this pixel (nearest)
      const uint8_t* p = &img[((size_t)y * W + x) * 4];
      int er = (10 + 30 * (int)c.ta + 7 * i) + (10 + 30 * (int)c.tb + 7 * i), eg = (5 + 20 * (int)c.ta) + (5 + 20 * (int)c.tb), eb = 4 * i;
      if (std::abs(p[0] - er) > 2 || std::abs(p[1] - eg) > 2 || std::abs(p[2] - eb) > 2) { if (!bad) std::printf("%s: px (%d,%d) got %d %d %d exp %d %d %d\n", c.name, x, y, p[0], p[1], p[2], er, eg, eb); bad++; }
    }
    std::printf("%s: %d wrong\n", c.name, bad); CHECK(bad == 0);
  }
  // extra paths: (A) t0 and t1 unbound while the shader samples t0 and t2: unbound reads as zeros; (B) s0 = clamp, s1 = wrap on two textures in one draw
  for (int variant = 0; variant < 2; variant++) {
    auto pb = ps_two(0, 0, 2, 1); auto psr = shader::dxbc_to_spirv(pb.data(), pb.size()); CHECK(psr.ok);
    d3d11::VulkanDrawBackend be(vk); be.SetShaders(vsr.words, psr.words);
    d3d11::Device dev; d3d11::DeviceContext ctx(dev); ctx.SetDrawBackend(&be); ctx.SetTexturePairing(psr.tex_sampler);
    auto rt = dev.CreateTexture2D(W, H, d3d11::Format::R8G8B8A8_UNORM); auto rtv = dev.CreateRenderTargetView(rt);
    d3d11::ResourceId tex[3];
    for (int k = 0; k < 3; k++) {
      uint8_t px[2 * 2 * 4];
      for (int i = 0; i < 4; i++) { px[i * 4] = (uint8_t)(10 + 30 * k + 7 * i); px[i * 4 + 1] = (uint8_t)(5 + 20 * k); px[i * 4 + 2] = (uint8_t)(2 * i); px[i * 4 + 3] = 255; }
      tex[k] = dev.CreateTexture2D(2, 2, d3d11::Format::R8G8B8A8_UNORM); CHECK(dev.UpdateTexture(tex[k], px, sizeof px));
    }
    if (variant == 1) CHECK(ctx.PSSetShaderResource(0, tex[0]));
    CHECK(ctx.PSSetShaderResource(2, tex[2]));
    CHECK(ctx.PSSetSampler(0, {false, false})); CHECK(ctx.PSSetSampler(1, {false, variant == 1}));
    ctx.IASetInputLayout(lay); ctx.IASetPrimitiveTopology(d3d11::Topology::TriangleList); ctx.RSSetViewport({0, 0, (float)W, (float)H}); ctx.OMSetRenderTarget(rtv);
    const float m = variant == 1 ? 2.0f : 1.0f;   // uv range 0..m
    VT a{-1, 1, 0, 1, 1, 1, 1, 1, 0, 0}, b{1, 1, 0, 1, 1, 1, 1, 1, m, 0}, cc{-1, -1, 0, 1, 1, 1, 1, 1, 0, m}, d{1, -1, 0, 1, 1, 1, 1, 1, m, m};
    std::vector<VT> q = {a, b, cc, b, d, cc};
    ctx.IASetVertexBuffer(dev.CreateBuffer(q.data(), q.size() * sizeof(VT)), sizeof(VT), 0);
    const float black[4] = {0, 0, 0, 1}; ctx.ClearRenderTargetView(rtv, black);
    if (!ctx.Draw((uint32_t)q.size(), 0)) { std::printf("variant %d: draw failed: %s\n", variant, ctx.last_error().c_str()); return 1; }
    std::vector<uint8_t> img; dev.ReadTexture(rt, &img);
    int bad = 0;
    for (int y = 1; y < H; y += 8) for (int x = 1; x < W; x += 8) {
      // texel index for nearest sampling at uv = (pixel centre / size) * m on a 2x2 texture, floor(uv*2)
      int cx = (int)std::floor((x + 0.5f) / W * m * 2), cy = (int)std::floor((y + 0.5f) / H * m * 2);
      int c0 = (std::min(cy, 1)) * 2 + std::min(cx, 1), c2 = (variant == 1 ? (cy & 1) * 2 + (cx & 1) : c0);
      int er = (variant == 1 ? 10 + 7 * c0 : 0) + (10 + 60 + 7 * c2), eg = (variant == 1 ? 5 : 0) + (5 + 40), eb = (variant == 1 ? 2 * c0 : 0) + 2 * c2;
      const uint8_t* p = &img[((size_t)y * W + x) * 4];
      if (std::abs(p[0] - er) > 2 || std::abs(p[1] - eg) > 2 || std::abs(p[2] - eb) > 2) { if (!bad) std::printf("variant %d: px (%d,%d) got %d %d %d exp %d %d %d\n", variant, x, y, p[0], p[1], p[2], er, eg, eb); bad++; }
    }
    std::printf("%s: %d wrong\n", variant == 0 ? "unbound t0/t1 read zeros" : "t0 clamp + t2 wrap, two samplers", bad); CHECK(bad == 0);
  }
  for (uint32_t slotno=0;slotno<4;++slotno) for (int mode=0;mode<8;++mode) {
    float lod = mode == 3 ? 1.5f : mode == 4 ? 0.f : mode == 5 ? 2.f : mode == 6 ? 0.f : mode == 7 ? 4.f : (float)mode;
    auto pb = dxb::ps_sample(dxb::swz(0,1,2,3),lod,slotno,0,false);
    // Add declarations before sample instructions through the builder's normal list construction.
    if(slotno) {
      auto code=std::vector<dxb::Words>{ins(98,{dst(dxb::T_INPUT,1,3)},false,2),ins(101,{dst(dxb::T_OUTPUT,0,15)}),dxb::dcl_tex2d(slotno),dxb::dcl_sampler(0),dxb::dcl_temps(1),ins(72,{dst(dxb::T_TEMP,0,15),src(dxb::T_INPUT,1,dxb::swz(0,1,0,0)),dxb::res_src(slotno),dxb::smp_src(0),dxb::imm1(lod)}),ins(dxb::OP_MOV,{dst(dxb::T_OUTPUT,0,15),src(dxb::T_TEMP,0)}),ins(dxb::OP_RET,{})};
      pb=dxb::shader(0,{{"TEXCOORD",0,0,1,3}},{{"SV_Target",0,0,0,15}},code);
    } else pb=dxb::ps_sample(dxb::swz(0,1,2,3),lod); auto psr = shader::dxbc_to_spirv(pb.data(),pb.size()); CHECK(psr.ok);
    d3d11::VulkanDrawBackend be(vk); be.SetShaders(vsr.words,psr.words);
    d3d11::Device dev; d3d11::DeviceContext ctx(dev); ctx.SetDrawBackend(&be); ctx.SetTexturePairing(psr.tex_sampler);
    auto tex=dev.CreateTexture2D(4,4,d3d11::Format::R8G8B8A8_UNORM,0); CHECK(tex && dev.texture(tex)->mips.size()==2);
    for(uint32_t l=0;l<3;++l) { std::vector<uint8_t> bytes((4u>>l)*(4u>>l)*4,0); for(size_t i=0;i<bytes.size();i+=4) {bytes[i+l]=255;bytes[i+3]=255;} CHECK(dev.UpdateTextureMip(tex,l,bytes.data(),bytes.size())); }
    if(mode==5) {
      std::vector<uint8_t> checker(64,0); for(size_t i=0;i<16;++i) {checker[i*4]=(i%2)?200:0;checker[i*4+1]=60;checker[i*4+3]=255;}
      CHECK(dev.UpdateTexture(tex,checker.data(),checker.size()) && dev.GenerateMips(tex));
    }
    CHECK(!dev.UpdateTextureMip(tex,3,"",1)); CHECK(!dev.CreateTexture2D(4,4,d3d11::Format::R8G8B8A8_UNORM,4));
    auto rt=dev.CreateTexture2D(W,H,d3d11::Format::R8G8B8A8_UNORM); auto rtv=dev.CreateRenderTargetView(rt);
    CHECK(ctx.PSSetShaderResource(slotno,tex,mode>=6 ? 1 : 0,mode>=6 ? 1 : ~0u)); d3d11::SamplerDesc sampler; sampler.mip_linear=mode==3; if(mode==4) sampler.min_lod=1; CHECK(ctx.PSSetSampler(0,sampler));
    ctx.IASetInputLayout(lay); ctx.IASetPrimitiveTopology(d3d11::Topology::TriangleList); ctx.RSSetViewport({0,0,(float)W,(float)H}); ctx.OMSetRenderTarget(rtv);
    VT a{-1,1,0,1,1,1,1,1,0,0},b{1,1,0,1,1,1,1,1,1,0},c{-1,-1,0,1,1,1,1,1,0,1},d{1,-1,0,1,1,1,1,1,1,1}; std::vector<VT> q={a,b,c,b,d,c};
    ctx.IASetVertexBuffer(dev.CreateBuffer(q.data(),q.size()*sizeof(VT)),sizeof(VT),0); const float black[4]={0,0,0,1}; CHECK(ctx.ClearRenderTargetView(rtv,black));
    if(!ctx.Draw(6,0)) {std::printf("mip error %s\n",ctx.last_error().c_str());return 1;}
    std::vector<uint8_t> img; CHECK(dev.ReadTexture(rt,&img)); auto* p=&img[((size_t)10*W+9)*4];
    int er=mode==5?100:mode==0?255:0,eg=mode>=6?255:mode==5?60:mode==1||mode==4?255:mode==3?128:0,eb=mode==2?255:mode==3?128:0;
    std::printf("mip t%u mode %d got %u %u %u\n",slotno,mode,p[0],p[1],p[2]); CHECK(std::abs(p[0]-er)<=1 && std::abs(p[1]-eg)<=1 && std::abs(p[2]-eb)<=1);
    if(std::getenv("ANYXBOX_MIP_PREVIEW") && mode==1) { FILE* f=std::fopen("/downloads/mip-green.ppm","wb");if(f){std::fprintf(f,"P6\n%d %d\n255\n",W,H);for(size_t k=0;k<img.size();k+=4) std::fwrite(img.data()+k,1,3,f);std::fclose(f);} }
  }
  std::printf("multi texture: all ok\n");
  return 0;
}
