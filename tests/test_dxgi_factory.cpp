// Anyxbox: dxgi.dll factory entry points (CreateDXGIFactory, CreateDXGIFactory1), called as a guest would, through the import registry.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "runtime/d3d11_runtime.h"
#include "syslayer/import_registry.h"
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
#if defined(__x86_64__) && !defined(_WIN32)
#define MS __attribute__((ms_abi))
#else
#define MS
#endif
typedef int32_t (MS *CreateFactoryFn)(const uint8_t*, void**);
typedef uint32_t (MS *RelFn)(void*);
typedef int32_t (MS *EnumFn)(void*, uint32_t, void**);
typedef int32_t (MS *AssocFn)(void*, void*, uint32_t);
typedef int32_t (MS *SwapFn)(void*, void*, const uint8_t*, void**);
typedef int32_t (MS *PresentFn)(void*, uint32_t, uint32_t);
int main() {
  anyxbox::D3D11Runtime rt; anyxbox::ImportRegistry reg; rt.register_imports(reg);
  using CreateDevice=int32_t(MS*)(void*,uint32_t,void*,uint32_t,void*,uint32_t,uint32_t,void**,void*,void**);
  void* dev=nullptr;void* ctx=nullptr;
  CHECK(((CreateDevice)(uintptr_t)reg.resolve("d3d11.dll","D3D11CreateDevice",0,false))(nullptr,1,nullptr,0,nullptr,0,7,&dev,nullptr,&ctx)==0);
  for (const char* name : {"CreateDXGIFactory", "CreateDXGIFactory1"}) {
    auto fn = (CreateFactoryFn)(uintptr_t)reg.resolve("dxgi.dll", name, 0, false);
    CHECK(fn);
    uint8_t iid[16] = {0};
    void* f = nullptr; CHECK(fn(iid, &f) == 0 && f);
    void** vt = *(void***)f; void* ad = (void*)1;
    CHECK(((EnumFn)vt[7])(f, 0, &ad) == (int32_t)0x887A0002);   // no adapters exposed: NOT_FOUND
    CHECK(((AssocFn)vt[8])(f, nullptr, 0) == 0);
    uint8_t desc[72] = {0}; uint32_t w = 64, h = 48; std::memcpy(desc, &w, 4); std::memcpy(desc + 4, &h, 4);   // format 0 -> RGBA8
    void* sc = (void*)1;CHECK(((SwapFn)vt[10])(f,nullptr,desc,&sc)!=0&&sc==nullptr);
    CHECK(((SwapFn)vt[10])(f,dev,desc,&sc)==0&&sc);
    int p0 = rt.presents;
    CHECK(((PresentFn)(*(void***)sc)[8])(sc, 0, 0) == 0 && rt.presents == p0 + 1 && rt.presented_w == 64 && rt.presented_h == 48);
    CHECK(((RelFn)(*(void***)sc)[2])(sc)==0);
    CHECK(((SwapFn)vt[10])(f, dev, nullptr, &sc) != 0&&sc==nullptr);   // null desc rejected
    CHECK(((RelFn)vt[2])(f) == 0);
  }
  CHECK(((CreateFactoryFn)(uintptr_t)reg.resolve("dxgi.dll", "CreateDXGIFactory", 0, false))(nullptr, nullptr) != 0);
  ((RelFn)(*(void***)ctx)[2])(ctx);((RelFn)(*(void***)dev)[2])(dev);
  std::printf("dxgi factory: all ok\n");
  return 0;
}
