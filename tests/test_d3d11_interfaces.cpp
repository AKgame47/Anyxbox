// Source-level ABI checks, independent Windows-header probe is fixtures/resource_interfaces.cpp.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "runtime/d3d11_runtime.h"
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
  uint32_t desc[18]={4,4};desc[4]=28;desc[7]=1;
  void* swap=nullptr;CHECK(((Swap)vt(factory)[10])(factory,dev,desc,&swap)==0);
  void* back=nullptr;CHECK(((GetBuffer)vt(swap)[9])(swap,0,ids[4],&back)==0&&back);
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
  for(auto* o:objects)((R)vt(o)[2])(o);
  puts("D3D11 interface identity, type, GetDevice, swapchain interface refusal and balanced reference paths ok");
}
