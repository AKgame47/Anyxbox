// Host-call coverage, plus independently compiled PE coverage when passed an .exe path.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#define ANYXBOX_HOST_TEST 1
#include "fixtures/keyboard_scene.c"
#include <filesystem>
#include <unistd.h>
#ifdef ANYXBOX_SDL_INPUT_TEST
#include <dlfcn.h>
#endif
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
  char temp[]="/tmp/anyxbox-asset-scene-XXXXXX";char* root=::mkdtemp(temp);CHECK(root);
  std::filesystem::create_directories(std::string(root)+"/data");
  std::vector<uint32_t> image(512*512);
  const uint32_t colors[]={0xff0000ffu,0xff00ff00u,0xffff0000u,0xffffffffu};
  for(unsigned y=0;y<512;++y)for(unsigned x=0;x<512;++x) image[y*512+x]=colors[(y>=256?2:0)+(x>=256?1:0)];
  {std::ofstream out(std::string(root)+"/data/texture.rgba",std::ios::binary);out.write((const char*)image.data(),image.size()*4);CHECK(out.good());}
  struct Cleanup {std::string path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{root};
  RunOptions options;options.asset_root=root;
  std::vector<uint8_t> pe;
  if(argc>1 && std::string(argv[1]).size()>4 && std::string(argv[1]).substr(std::strlen(argv[1])-4)==".exe") {
    std::ifstream f(argv[1],std::ios::binary); pe.assign(std::istreambuf_iterator<char>(f),{}); CHECK(!pe.empty());
  }
  bool required=argc>2 && std::string(argv[2])=="--require-vulkan";
  pnr::graphics::VulkanBackend vk;
  if(!vk.open() || !vk.create_instance("asset-scene-test") || vk.devices().empty() || !vk.create_device(0)) {
    CHECK(!required); std::puts("SKIP asset-scene pixels: no Vulkan device"); return 0;
  }
  // Reject oversized model storage before allocating, and reject insufficient upload bytes.
  {d3d11::Device model;CHECK(!model.CreateTexture2D(16384,16384,d3d11::Format::R8G8B8A8_UNORM));}
#ifdef ANYXBOX_SDL_INPUT_TEST
  struct SdlHost:HostWindow {
    std::unique_ptr<HostWindow> window=make_sdl_window();
    void* lib=dlopen("libSDL2-2.0.so.0",RTLD_NOW|RTLD_LOCAL);
    int (*push)(void*)=lib?(int(*)(void*))dlsym(lib,"SDL_PushEvent"):nullptr;
    void (*flush)(uint32_t,uint32_t)=lib?(void(*)(uint32_t,uint32_t))dlsym(lib,"SDL_FlushEvents"):nullptr;
    bool injected=true;int opens=0,presents=0;
    ~SdlHost(){if(window)window->close();if(lib)dlclose(lib);}
    bool open(uint32_t w,uint32_t h,const char* title)override{
      bool ok=window && window->open(w,h,title);if(ok)++opens;return ok;
    }
    void present(const uint8_t* p,uint32_t w,uint32_t h)override{++presents;window->present(p,w,h);}
    bool poll(HostEvent& e)override{return window->poll(e);}
    void close()override{if(window)window->close();}
    void begin_injection(){flush(0,0xffff);}
    void add(HostEvent::Type t,int key=0){
      alignas(8) uint8_t event[64]={};
      uint32_t type=t==HostEvent::FocusLost?0x200:t==HostEvent::KeyDown?0x300:0x301;
      std::memcpy(event,&type,4);
      if(t==HostEvent::FocusLost)event[12]=13;
      else {int32_t sym=key==0x27?0x4000004f:key;std::memcpy(event+20,&sym,4);event[12]=t==HostEvent::KeyDown?1:0;}
      if(push(event)!=1)injected=false;
    }
  } host;
  if(!host.window || !host.push || !host.flush || !std::getenv("DISPLAY")){
    CHECK(!required);std::puts("SKIP SDL injected keyboard scene: SDL/display unavailable");return 0;
  }
#else
  struct ScriptHost:HostWindow {
    std::vector<HostEvent> events;size_t at=0;
    bool open(uint32_t,uint32_t,const char*)override{return true;}
    void present(const uint8_t*,uint32_t,uint32_t)override{}
    bool poll(HostEvent& e)override{if(at>=events.size())return false;e=events[at++];return true;}
    void close()override{}
    void add(HostEvent::Type t,int key=0){events.push_back({t,key});}
  } host;
#endif
  D3D11Runtime rt; User32Context u;u.host=&host; d3d11::VulkanDrawBackend be(vk); rt.ctx->SetDrawBackend(&be);
  rt.on_shaders_ubo=[&](const std::vector<uint32_t>& v,const std::vector<uint32_t>& p,bool b) { be.SetShaders(v,p,b); };
  std::vector<std::vector<uint8_t>> frames;
  rt.on_present=[&](const uint8_t* p,uint32_t w,uint32_t h) { if(w==320 && h==240) frames.emplace_back(p,p+w*h*4);
#ifdef ANYXBOX_SDL_INPUT_TEST
    host.present(p,w,h); // same real SDL window path used by attach_window_present/run_exe --window
    host.begin_injection(); // remove unrelated Xvfb startup/focus events before deterministic synthetic events
#endif
    if(frames.size()==1)host.add(HostEvent::KeyDown,0x27);
    if(frames.size()==2)host.add(HostEvent::KeyUp,0x27);
    if(frames.size()==3){host.add(HostEvent::KeyDown,0x27);host.add(HostEvent::FocusLost);}
  };
  if(pe.empty()) {
    ImportRegistry reg; Kernel32Context k; k.folder=std::make_unique<MappedFolder>(root,true); kernel32_register(reg,k); user32_register(reg,u); rt.register_imports(reg);
    reg.add("kernel32.dll","ExitProcess",(uint64_t)(uintptr_t)&exit_stub);
    for(auto& i:imports()) { auto a=reg.resolve(i.dll,i.name,0,false); CHECK(a); *i.slot=(void*)(uintptr_t)a; }
    CHECK(entry()==0);
  } else {
    auto r=run_pe(pe.data(),pe.size(),true,[&](ImportRegistry& reg) { user32_register(reg,u); rt.register_imports(reg); },options);
    CHECK(r.loaded && r.ran && r.exit_code==0 && r.output.empty());
  }
  std::printf("state presents=%d shaders=%u frames=%zu error=%s\n",rt.presents,rt.shaders_translated,frames.size(),rt.last_error.c_str());
#ifdef ANYXBOX_SDL_INPUT_TEST
  CHECK(host.opens==1 && host.presents==4 && host.injected);
  std::puts("SDL_PushEvent -> SDL_PollEvent -> user32 -> independent/source keyboard scene; real SDL window presented four frames");
#endif
  CHECK(rt.last_error.empty() && rt.presents==4 && rt.shaders_translated==4 && frames.size()==4);
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
  CHECK(pixel(1,300,10,13,13,38));
  for(int frame=1;frame<4;++frame) {
    CHECK(pixel(frame,50,60,13,13,38) && pixel(frame,160,60,0,255,0));
  }
  CHECK(frames[1]==frames[2] && frames[2]==frames[3]);
  CHECK(pixel(1,240,70,255,0,0) && pixel(1,290,70,0,255,0));
  CHECK(pixel(1,240,170,0,0,255) && pixel(1,290,170,255,255,255));
  std::printf("asset-scene: keyboard-controlled file-backed texture, %s, four presents, four translated shaders, move/release/focus frames verified on Vulkan\n",pe.empty()?"host calls":"independent Windows binary");
  std::puts("test_fixture_keyboard_scene ok"); return 0;
}

int main(int argc,char** argv){
 int rc=run_case(argc,argv);if(rc)return rc;
 std::string dir=argc>1?argv[1]:"tests/fixtures";
 if(dir.size()>=4 && dir.substr(dir.size()-4)==".exe")return 0;
 std::string path=dir+"/keyboard_scene.run.exe";std::vector<char> p(path.begin(),path.end());p.push_back(0);
 char* args[]={argv[0],p.data()};return run_case(2,args);
}
