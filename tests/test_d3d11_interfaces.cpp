// Source-level ABI checks, independent Windows-header probe is fixtures/resource_interfaces.cpp.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "runtime/d3d11_runtime.h"
#include "fixtures/triangle_shaders.h"
#define MS __attribute__((ms_abi))
#define CHECK(c) do{if(!(c)){printf("FAIL %d: %s\n",__LINE__,#c);return 1;}}while(0)
using Q=int32_t(MS*)(void*,const uint8_t*,void**);
using R=uint32_t(MS*)(void*);
using GD=void(MS*)(void*,void**);
using GT=void(MS*)(void*,uint32_t*);
using CB=int32_t(MS*)(void*,const uint32_t*,void*,void**);
using Create=int32_t(MS*)(void*,uint32_t,void*,uint32_t,void*,uint32_t,uint32_t,void**,void*,void**);
static void** vt(void* p){return *(void***)p;}
int main(){
  anyxbox::D3D11Runtime rt;anyxbox::ImportRegistry reg;rt.register_imports(reg);
  void* dev=nullptr;void* ctx=nullptr;
  auto create=(Create)(uintptr_t)reg.resolve("d3d11.dll","D3D11CreateDevice",0,false);
  CHECK(create(nullptr,1,nullptr,0,nullptr,0,7,&dev,nullptr,&ctx)==0);
  const uint8_t ids[][16]={
    {0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46},
    {0xc8,0xe5,0x41,0x18,0xb0,0x16,0x9b,0x48,0xbc,0xc8,0x44,0xcf,0xb0,0xd5,0xde,0xae},
    {0xf3,0x63,0x8e,0xdc,0x2b,0xd1,0x52,0x49,0xb4,0x7b,0x5e,0x45,0x02,0x6a,0x86,0x2d},
    {0x85,0x0b,0x57,0x48,0xee,0xd1,0xcd,0x4f,0xa2,0x50,0xeb,0x35,0x07,0x22,0xb0,0x37},
    {0xf2,0xaa,0x15,0x6f,0x08,0xd2,0x89,0x4e,0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c},
    {0xdb,0x6d,0x6f,0xdb,0x77,0xac,0x88,0x4e,0x82,0x53,0x81,0x9d,0xf9,0xbb,0xf1,0x40},
    {0x6c,0xa9,0xbf,0xc0,0x89,0xe0,0xfb,0x44,0x8e,0xaf,0x26,0xf8,0x79,0x61,0x90,0xda}};
  uint32_t bd[]={16,0,1,0,0,0};uint32_t td[]={4,4,1,1,28,1,0,0,8,0,0};
  void* buf=nullptr;void* tex=nullptr;void* depth=nullptr;
  CHECK(((CB)vt(dev)[3])(dev,bd,nullptr,&buf)==0);
  CHECK(((CB)vt(dev)[5])(dev,td,nullptr,&tex)==0);
  td[4]=40;td[8]=0x40;CHECK(((CB)vt(dev)[5])(dev,td,nullptr,&depth)==0);
  using Desc=void(MS*)(void*,void*);
  uint32_t outdesc[12];for(auto& x:outdesc)x=0xdeadbeefu;
  ((Desc)vt(buf)[10])(buf,outdesc);CHECK(std::memcmp(outdesc,bd,sizeof(bd))==0&&outdesc[6]==0xdeadbeefu);
  auto checktex=[&](void* obj,const uint32_t* expected){
    for(auto& x:outdesc)x=0xdeadbeefu;
    ((Desc)vt(obj)[10])(obj,outdesc);
    return std::memcmp(outdesc,expected,44)==0&&outdesc[11]==0xdeadbeefu;
  };
  CHECK(checktex(depth,td));td[4]=28;td[8]=8;CHECK(checktex(tex,td));
  td[2]=0;td[7]=3;td[8]=0;td[9]=0x20000;void* staging=nullptr;
  CHECK(((CB)vt(dev)[5])(dev,td,nullptr,&staging)==0);td[2]=3;CHECK(checktex(staging,td));
  ((Desc)vt(staging)[10])(staging,nullptr);((R)vt(staging)[2])(staging);
  void* objects[]={dev,ctx,buf,tex,depth};
  for(unsigned k=0;k<5;++k){
    void* obj=objects[k];uint32_t refs=((R)vt(obj)[1])(obj);CHECK(((R)vt(obj)[2])(obj)==refs-1);
    for(unsigned i=0;i<7;++i){
      bool yes=i==0 || (i==1&&k>0) || (i==2&&k>=2) || (i==3&&k==2) || (i==4&&k>=3) || (i==5&&k==0) || (i==6&&k==1);
      void* out=(void*)1;auto hr=((Q)vt(obj)[0])(obj,ids[i],&out);
      if(yes){CHECK(hr==0&&out==obj);CHECK(((R)vt(out)[2])(out)==refs-1);}
      else CHECK(hr==(int32_t)0x80004002&&out==nullptr);
    }
    void* out=(void*)1;CHECK(((Q)vt(obj)[0])(obj,nullptr,&out)==(int32_t)0x80004002&&out==nullptr);
    CHECK(((Q)vt(obj)[0])(obj,ids[0],nullptr)==(int32_t)0x80070057);
    CHECK(((R)vt(obj)[1])(obj)==refs);CHECK(((R)vt(obj)[2])(obj)==refs-1);
    if(k>0){out=nullptr;uint32_t dr=((R)vt(dev)[1])(dev);((R)vt(dev)[2])(dev);((GD)vt(obj)[3])(obj,&out);CHECK(out==dev);CHECK(((R)vt(out)[2])(out)==dr-1);}
    if(k>=2){uint32_t type=99;((GT)vt(obj)[7])(obj,&type);CHECK(type==(k==2?1u:3u));}
  }
  using Factory=int32_t(MS*)(const uint8_t*,void**);
  using Swap=int32_t(MS*)(void*,void*,const uint32_t*,void**);
  using GetBuffer=int32_t(MS*)(void*,uint32_t,const uint8_t*,void**);
  void* factory=nullptr;CHECK(((Factory)(uintptr_t)reg.resolve("dxgi.dll","CreateDXGIFactory",0,false))(ids[0],&factory)==0);
  uint32_t desc[18]={4,4};desc[4]=87;desc[7]=1;desc[9]=0x30;
  void* swap=nullptr;CHECK(((Swap)vt(factory)[10])(factory,dev,desc,&swap)==0);
  void* back=nullptr;CHECK(((GetBuffer)vt(swap)[9])(swap,0,ids[4],&back)==0&&back);
  const uint32_t backdesc[]={4,4,1,1,87,1,0,0,0x28,0,0};CHECK(checktex(back,backdesc));
  uint32_t br=((R)vt(back)[1])(back);((R)vt(back)[2])(back);
  for(unsigned i=0;i<7;++i){
    void* out=(void*)1;auto hr=((GetBuffer)vt(swap)[9])(swap,0,ids[i],&out);
    if(i==0||i==1||i==2||i==4){CHECK(hr==0&&out==back);CHECK(((R)vt(out)[2])(out)==br-1);}
    else CHECK(hr==(int32_t)0x80004002&&out==nullptr);
  }
  void* out=(void*)1;CHECK(((GetBuffer)vt(swap)[9])(swap,1,ids[4],&out)==(int32_t)0x80070057&&out==nullptr);
  out=(void*)1;CHECK(((GetBuffer)vt(swap)[9])(swap,0,nullptr,&out)==(int32_t)0x80004002&&out==nullptr);
  CHECK(((GetBuffer)vt(swap)[9])(swap,0,ids[4],nullptr)==(int32_t)0x80070057);
  ((R)vt(back)[2])(back);((R)vt(swap)[2])(swap);((R)vt(factory)[2])(factory);
  // Views retain resource identity across release of the original caller reference.
  for(unsigned slot:{7u,9u,10u}){
    td[2]=1;td[7]=0;td[8]=slot==10?0x40:0x28;td[9]=0;td[4]=slot==10?40:28;
    void* resource=nullptr;CHECK(((CB)vt(dev)[5])(dev,td,nullptr,&resource)==0);
    void* view=nullptr;CHECK(((CB)vt(dev)[slot])(dev,(const uint32_t*)resource,nullptr,&view)==0);
    CHECK(((R)vt(resource)[2])(resource)==1); // only the view owns it now
    void* returned=nullptr;((GD)vt(view)[7])(view,&returned);CHECK(returned==resource);
    CHECK(((R)vt(view)[2])(view)==0); // GetResource must keep resource alive after view release
    uint32_t type=0;((GT)vt(returned)[7])(returned,&type);CHECK(type==3);
    CHECK(((R)vt(returned)[2])(returned)==0);
  }
  puts("view GetResource: RTV/SRV/DSV retain original resource and returned reference outlives view");
  using BindTargets=void(MS*)(void*,uint32_t,void* const*,void*);
  auto bindTargets=(BindTargets)vt(ctx)[33];
  void* targetViews[2][2]={};
  for(unsigned pair=0;pair<2;++pair)for(unsigned kind=0;kind<2;++kind){
    td[4]=kind?40:28;td[8]=kind?0x40:0x20;
    void* resource=nullptr;CHECK(((CB)vt(dev)[5])(dev,td,nullptr,&resource)==0);
    CHECK(((CB)vt(dev)[kind?10:9])(dev,(const uint32_t*)resource,nullptr,&targetViews[pair][kind])==0);
    CHECK(((R)vt(resource)[2])(resource)==1);
  }
  auto refs=[](void* o){uint32_t n=((R)vt(o)[1])(o);((R)vt(o)[2])(o);return n-1;};
  bindTargets(ctx,1,&targetViews[0][0],targetViews[0][1]);
  for(auto* view:targetViews[0])CHECK(refs(view)==2);
  bindTargets(ctx,1,&targetViews[0][0],targetViews[0][1]);
  for(auto* view:targetViews[0])CHECK(refs(view)==2);
  bindTargets(ctx,2,&targetViews[1][0],targetViews[1][1]);
  CHECK(!rt.last_error.empty());rt.last_error.clear(); // refusal must leave old bindings intact
  for(auto* view:targetViews[0])CHECK(refs(view)==2);
  for(auto* view:targetViews[1])CHECK(refs(view)==1);
  bindTargets(ctx,1,&targetViews[1][0],targetViews[1][1]);
  for(auto* view:targetViews[0])CHECK(((R)vt(view)[2])(view)==0);
  for(auto* view:targetViews[1])CHECK(((R)vt(view)[2])(view)==1); // caller release
  bindTargets(ctx,1,&targetViews[1][0],targetViews[1][1]); // solely context-owned rebind
  for(auto* view:targetViews[1])CHECK(((R)vt(view)[1])(view)==2);
  bindTargets(ctx,0,nullptr,nullptr);
  for(auto* view:targetViews[1])CHECK(((R)vt(view)[2])(view)==0);
  void* teardownViews[2]={};R viewRelease=nullptr;
  {anyxbox::D3D11Runtime scoped;anyxbox::ImportRegistry sr;scoped.register_imports(sr);void* d=nullptr;void* c=nullptr;
    auto makeDevice=(Create)(uintptr_t)sr.resolve("d3d11.dll","D3D11CreateDevice",0,false);
    CHECK(makeDevice(nullptr,1,nullptr,0,nullptr,0,7,&d,nullptr,&c)==0);
    for(unsigned kind=0;kind<2;++kind){
      td[4]=kind?40:28;td[8]=kind?0x40:0x20;void* resource=nullptr;
      CHECK(((CB)vt(d)[5])(d,td,nullptr,&resource)==0);
      CHECK(((CB)vt(d)[kind?10:9])(d,(const uint32_t*)resource,nullptr,&teardownViews[kind])==0);
      CHECK(((R)vt(resource)[2])(resource)==1);
    }
    viewRelease=(R)vt(teardownViews[0])[2];
    ((BindTargets)vt(c)[33])(c,1,&teardownViews[0],teardownViews[1]);
  }
  // Static Release only, not guest calls through runtime-owned vtables after teardown.
  for(auto* view:teardownViews)CHECK(viewRelease(view)==0);
  puts("output merger: RTV/DSV retain, rebind, replacement, caller release, unbind, teardown and MRT refusal ok");
  using BindResources=void(MS*)(void*,uint32_t,uint32_t,void* const*);
  auto bindResources=(BindResources)vt(ctx)[8];void* srvs[2]={};
  for(auto& srv:srvs){
    td[4]=28;td[8]=8;void* resource=nullptr;
    CHECK(((CB)vt(dev)[5])(dev,td,nullptr,&resource)==0);
    CHECK(((CB)vt(dev)[7])(dev,(const uint32_t*)resource,nullptr,&srv)==0);
    CHECK(((R)vt(resource)[2])(resource)==1);
  }
  bindResources(ctx,0,2,srvs);
  for(auto* srv:srvs)CHECK(((R)vt(srv)[2])(srv)==1); // only context owns each view
  void* swapped[]={srvs[1],srvs[0]};bindResources(ctx,0,2,swapped);
  for(auto* srv:srvs)CHECK(refs(srv)==1);
  bindResources(ctx,0,2,swapped);for(auto* srv:srvs)CHECK(refs(srv)==1);
  bindResources(ctx,3,2,swapped);CHECK(!rt.last_error.empty());rt.last_error.clear();
  bindResources(ctx,0xffffffffu,2,swapped);CHECK(!rt.last_error.empty());rt.last_error.clear();
  for(auto* srv:srvs)CHECK(refs(srv)==1); // invalid range has no effects
  for(auto* srv:srvs)CHECK(((R)vt(srv)[1])(srv)==2);
  bindResources(ctx,1,1,nullptr);CHECK(refs(srvs[0])==1&&refs(srvs[1])==2);
  bindResources(ctx,3,1,&srvs[0]);CHECK(refs(srvs[0])==2); // high supported slot
  bindResources(ctx,0,1,&srvs[0]);CHECK(refs(srvs[1])==1&&refs(srvs[0])==3);
  bindResources(ctx,0,4,nullptr);
  for(auto* srv:srvs)CHECK(((R)vt(srv)[2])(srv)==0);
  void* teardownSRV=nullptr;R srvRelease=nullptr;
  {anyxbox::D3D11Runtime scoped;anyxbox::ImportRegistry sr;scoped.register_imports(sr);void* d=nullptr;void* c=nullptr;
    auto makeDevice=(Create)(uintptr_t)sr.resolve("d3d11.dll","D3D11CreateDevice",0,false);
    CHECK(makeDevice(nullptr,1,nullptr,0,nullptr,0,7,&d,nullptr,&c)==0);
    td[4]=28;td[8]=8;void* resource=nullptr;
    CHECK(((CB)vt(d)[5])(d,td,nullptr,&resource)==0);
    CHECK(((CB)vt(d)[7])(d,(const uint32_t*)resource,nullptr,&teardownSRV)==0);
    ((R)vt(resource)[2])(resource);srvRelease=(R)vt(teardownSRV)[2];
    void* repeat[]={teardownSRV,teardownSRV};((BindResources)vt(c)[8])(c,2,2,repeat);
  }
  CHECK(srvRelease(teardownSRV)==0); // static Release only, runtime-owned vtable no longer valid
  puts("pixel SRV bindings: caller release, swap, rebind, partial replacement, unbind, range refusal and teardown ok");
  using BindVB=void(MS*)(void*,uint32_t,uint32_t,void* const*,const uint32_t*,const uint32_t*);
  using BindIB=void(MS*)(void*,void*,uint32_t,uint32_t);
  auto bindVB=(BindVB)vt(ctx)[18];auto bindIB=(BindIB)vt(ctx)[19];
  void* inputs[2]={};uint32_t strides[]={16,16},offsets[]={0,0};bd[2]=3;
  for(auto& input:inputs)CHECK(((CB)vt(dev)[3])(dev,bd,nullptr,&input)==0);
  bindVB(ctx,0,2,inputs,strides,offsets);
  for(auto* input:inputs)CHECK(((R)vt(input)[2])(input)==1);
  void* reversed[]={inputs[1],inputs[0]};
  bindVB(ctx,0,2,reversed,strides,offsets);bindVB(ctx,0,2,reversed,strides,offsets);
  for(auto* input:inputs)CHECK(refs(input)==1);
  bindVB(ctx,15,2,reversed,strides,offsets);CHECK(!rt.last_error.empty());rt.last_error.clear();
  bindVB(ctx,0xffffffffu,2,reversed,strides,offsets);CHECK(!rt.last_error.empty());rt.last_error.clear();
  for(auto* input:inputs)CHECK(refs(input)==1);
  for(auto* input:inputs)CHECK(((R)vt(input)[1])(input)==2);
  bindVB(ctx,1,1,nullptr,strides,offsets);CHECK(refs(inputs[0])==1&&refs(inputs[1])==2);
  bindVB(ctx,15,1,&inputs[0],strides,offsets);CHECK(refs(inputs[0])==2);
  bindVB(ctx,0,1,nullptr,strides,offsets);CHECK(refs(inputs[1])==1);
  bindVB(ctx,15,1,nullptr,strides,offsets);CHECK(refs(inputs[0])==1);
  bindIB(ctx,inputs[0],57,0);bindIB(ctx,inputs[0],57,0);CHECK(refs(inputs[0])==2);
  bindIB(ctx,inputs[1],28,0);CHECK(!rt.last_error.empty());rt.last_error.clear();
  CHECK(refs(inputs[0])==2&&refs(inputs[1])==1);
  bindIB(ctx,inputs[1],42,0);CHECK(refs(inputs[0])==1&&refs(inputs[1])==2);
  CHECK(((R)vt(inputs[1])[2])(inputs[1])==1);bindIB(ctx,inputs[1],42,0);
  CHECK(((R)vt(inputs[1])[1])(inputs[1])==2);bindIB(ctx,nullptr,57,0);
  for(auto* input:inputs)CHECK(((R)vt(input)[2])(input)==0);
  void* teardownBuffer=nullptr;R bufferRelease=nullptr;
  {anyxbox::D3D11Runtime scoped;anyxbox::ImportRegistry sr;scoped.register_imports(sr);void* d=nullptr;void* c=nullptr;
    auto makeDevice=(Create)(uintptr_t)sr.resolve("d3d11.dll","D3D11CreateDevice",0,false);
    CHECK(makeDevice(nullptr,1,nullptr,0,nullptr,0,7,&d,nullptr,&c)==0);
    CHECK(((CB)vt(d)[3])(d,bd,nullptr,&teardownBuffer)==0);bufferRelease=(R)vt(teardownBuffer)[2];
    void* repeat[]={teardownBuffer,teardownBuffer};
    ((BindVB)vt(c)[18])(c,14,2,repeat,strides,offsets);((BindIB)vt(c)[19])(c,teardownBuffer,57,0);
  }
  CHECK(bufferRelease(teardownBuffer)==0); // static Release only after runtime destruction
  puts("input buffers: caller release, vertex swap, rebind, partial replacement, index replacement, refusal, unbind and teardown ok");
  for(unsigned stage:{7u,16u}){
    auto bindCB=(BindResources)vt(ctx)[stage];void* constants[2]={};bd[2]=4;
    for(auto& cb:constants)CHECK(((CB)vt(dev)[3])(dev,bd,nullptr,&cb)==0);
    bindCB(ctx,0,2,constants);for(auto* cb:constants)CHECK(((R)vt(cb)[2])(cb)==1);
    void* swapped[]={constants[1],constants[0]};bindCB(ctx,0,2,swapped);bindCB(ctx,0,2,swapped);
    for(auto* cb:constants)CHECK(refs(cb)==1);
    bindCB(ctx,13,2,swapped);CHECK(!rt.last_error.empty());rt.last_error.clear();
    bindCB(ctx,0xffffffffu,2,swapped);CHECK(!rt.last_error.empty());rt.last_error.clear();
    for(auto* cb:constants)CHECK(refs(cb)==1);
    for(auto* cb:constants)CHECK(((R)vt(cb)[1])(cb)==2);
    bindCB(ctx,1,1,nullptr);CHECK(refs(constants[0])==1&&refs(constants[1])==2);
    bindCB(ctx,13,1,&constants[0]);CHECK(refs(constants[0])==2);
    bindCB(ctx,0,1,&constants[0]);CHECK(refs(constants[0])==3&&refs(constants[1])==1);
    bindCB(ctx,0,14,nullptr);for(auto* cb:constants)CHECK(((R)vt(cb)[2])(cb)==0);
  }
  void* teardownCB=nullptr;R cbRelease=nullptr;
  {anyxbox::D3D11Runtime scoped;anyxbox::ImportRegistry sr;scoped.register_imports(sr);void* d=nullptr;void* c=nullptr;
    auto makeDevice=(Create)(uintptr_t)sr.resolve("d3d11.dll","D3D11CreateDevice",0,false);
    CHECK(makeDevice(nullptr,1,nullptr,0,nullptr,0,7,&d,nullptr,&c)==0);
    CHECK(((CB)vt(d)[3])(d,bd,nullptr,&teardownCB)==0);cbRelease=(R)vt(teardownCB)[2];
    void* repeat[]={teardownCB,teardownCB};
    ((BindResources)vt(c)[7])(c,12,2,repeat);((BindResources)vt(c)[16])(c,12,2,repeat);
    CHECK(refs(teardownCB)==5);
  }
  CHECK(cbRelease(teardownCB)==0); // static Release only, not a post-runtime guest call
  puts("constant buffers: VS/PS caller release, swap, rebind, partial replacement, range refusal, unbind and teardown ok");
  using Shader=int32_t(MS*)(void*,const void*,size_t,void*,void**);
  using BindShader=void(MS*)(void*,void*,void*,uint32_t);
  for(unsigned stage:{11u,9u}){
    void* first=nullptr;void* second=nullptr;unsigned createSlot=stage==11?12:15;
    const void* code=stage==11?(const void*)triangle_vs:triangle_ps;size_t bytes=stage==11?sizeof(triangle_vs):sizeof(triangle_ps);
    CHECK(((Shader)vt(dev)[createSlot])(dev,code,bytes,nullptr,&first)==0);
    CHECK(((Shader)vt(dev)[createSlot])(dev,code,bytes,nullptr,&second)==0);
    auto bind=(BindShader)vt(ctx)[stage];bind(ctx,first,nullptr,0);
    CHECK(((R)vt(first)[1])(first)==3);CHECK(((R)vt(first)[2])(first)==2);
    bind(ctx,first,nullptr,0);CHECK(((R)vt(first)[1])(first)==3);((R)vt(first)[2])(first);
    bind(ctx,second,nullptr,0);CHECK(((R)vt(first)[2])(first)==0);
    bind(ctx,nullptr,nullptr,0);CHECK(((R)vt(second)[2])(second)==0);
  }
  // Runtime teardown must drop bound shader references even without explicit unbind.
  void* surviving[2]={};R finalRelease=nullptr;
  {anyxbox::D3D11Runtime scoped;anyxbox::ImportRegistry sr;scoped.register_imports(sr);void* d=nullptr;void* c=nullptr;
    auto makeDevice=(Create)(uintptr_t)sr.resolve("d3d11.dll","D3D11CreateDevice",0,false);
    CHECK(makeDevice(nullptr,1,nullptr,0,nullptr,0,7,&d,nullptr,&c)==0);
    CHECK(((Shader)vt(d)[12])(d,triangle_vs,sizeof(triangle_vs),nullptr,&surviving[0])==0);
    CHECK(((Shader)vt(d)[15])(d,triangle_ps,sizeof(triangle_ps),nullptr,&surviving[1])==0);
    finalRelease=(R)vt(surviving[0])[2];
    ((BindShader)vt(c)[11])(c,surviving[0],nullptr,0);((BindShader)vt(c)[9])(c,surviving[1],nullptr,0);
  }
  for(auto* shader:surviving)CHECK(finalRelease(shader)==0);
  puts("shader bindings: retain, same-object rebind, replacement, unbind and runtime teardown references ok");
  for(auto* o:objects)((R)vt(o)[2])(o);
  puts("resource GetDesc: buffer/color/depth/staging/full-mip/backbuffer fields and output extents ok");
  puts("D3D11 interface identity, type, GetDevice, swapchain interface refusal and balanced reference paths ok");
}
