// Host-call coverage, plus independently compiled PE coverage when passed an .exe path.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#define ANYXBOX_HOST_TEST 1
#include "fixtures/draw_copy.c"
#include "gfx/d3d11_vulkan.h"
#include "runtime/kernel32.h"
#include "runtime/d3d11_runtime.h"
#include "runtime/run_pe.h"
#include "runtime/user32.h"
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } } while(0)
using namespace anyxbox;
struct Import { const char* dll; const char* name; void** slot; };
static std::vector<Import>& imports() { static std::vector<Import> v; return v; }
extern "C" void anyxbox_fixture_import(const char* dll,const char* name,void** slot) { imports().push_back({dll,name,slot}); }
static __attribute__((ms_abi)) void exit_stub(uint32_t) {}
static int run_case(int argc,char** argv) {
  std::vector<uint8_t> pe;
  if(argc>1 && std::string(argv[1]).size()>4 && std::string(argv[1]).substr(std::strlen(argv[1])-4)==".exe") {
    std::ifstream f(argv[1],std::ios::binary); pe.assign(std::istreambuf_iterator<char>(f),{}); CHECK(!pe.empty());
  }
  bool required=argc>2 && std::string(argv[2])=="--require-vulkan";
  pnr::graphics::VulkanBackend vk;
  if(!vk.open() || !vk.create_instance("draw-copy-test") || vk.devices().empty() || !vk.create_device(0)) {
    CHECK(!required); std::puts("SKIP draw-copy pixels: no Vulkan device"); return 0;
  }
  D3D11Runtime rt; User32Context u; d3d11::VulkanDrawBackend be(vk); rt.ctx->SetDrawBackend(&be);
  rt.on_shaders_ubo=[&](const std::vector<uint32_t>& v,const std::vector<uint32_t>& p,bool b) { be.SetShaders(v,p,b); };
  std::vector<std::vector<uint8_t>> frames;
  rt.on_present=[&](const uint8_t* p,uint32_t w,uint32_t h) { if(w==320 && h==240) frames.emplace_back(p,p+w*h*4); };
  if(pe.empty()) {
    ImportRegistry reg; Kernel32Context k; kernel32_register(reg,k); user32_register(reg,u); rt.register_imports(reg);
    reg.add("kernel32.dll","ExitProcess",(uint64_t)(uintptr_t)&exit_stub);
    for(auto& i:imports()) { auto a=reg.resolve(i.dll,i.name,0,false); CHECK(a); *i.slot=(void*)(uintptr_t)a; }
    CHECK(entry()==0);
  } else {
    auto r=run_pe(pe.data(),pe.size(),true,[&](ImportRegistry& reg) { user32_register(reg,u); rt.register_imports(reg); });
    CHECK(r.loaded && r.ran && r.exit_code==0 && r.output.empty());
  }
  std::printf("state presents=%d shaders=%u frames=%zu error=%s\n",rt.presents,rt.shaders_translated,frames.size(),rt.last_error.c_str());
  CHECK(rt.last_error.empty() && rt.presents==2 && rt.shaders_translated==4 && frames.size()==2);
  if(argc>3) for(size_t i=0;i<frames.size();++i) {
    std::ofstream out(std::string(argv[3])+std::to_string(i)+".ppm",std::ios::binary);
    CHECK(out); out<<"P6\n320 240\n255\n";
    for(size_t p=0;p<frames[i].size();p+=4) out.write((const char*)&frames[i][p],3);
    CHECK(out.good());
  }
  auto pixel=[&](int frame,int x,int y,int r,int g,int b) {
    auto p=&frames[frame][((size_t)y*320+x)*4];
    return std::abs((int)p[0]-r)<=4 && std::abs((int)p[1]-g)<=4 && std::abs((int)p[2]-b)<=4 && p[3]>=250;
  };
  CHECK(pixel(0,300,10,13,13,38));
  CHECK(pixel(0,240,70,255,0,0) && pixel(0,290,70,0,255,0));
  CHECK(pixel(0,240,170,0,0,255) && pixel(0,290,170,255,255,255));
  CHECK(pixel(1,300,10,0,255,255) && pixel(1,60,190,0,255,255));
  CHECK(pixel(1,50,60,0,255,0) && pixel(1,190,150,255,128,0) && pixel(1,120,100,0,255,0));
  CHECK(pixel(1,240,70,255,255,0) && pixel(1,290,70,255,255,0));
  CHECK(pixel(1,240,170,255,255,0) && pixel(1,290,170,255,255,0));
  std::printf("draw-copy: %s, two presents, four translated shaders, both frames verified on Vulkan\n",pe.empty()?"host calls":"independent Windows binary");
  std::puts("test_fixture_draw_copy ok"); return 0;
}

int main(int argc,char** argv) {
  int rc=run_case(argc,argv);
  if(rc) return rc;
  // A directory invocation covers both source calls and the retained independent PE.
  std::string dir=argc>1?argv[1]:"tests/fixtures";
  if(dir.size()>=4 && dir.substr(dir.size()-4)==".exe") return 0;
  std::string exe=dir+"/draw_copy.run.exe";
  std::vector<char> path(exe.begin(),exe.end()); path.push_back(0);
  char* args[]={argv[0],path.data()};
  return run_case(2,args);
}
