#include "runtime/host_window.h"
#include <cstring>
#if defined(__linux__)
#include <dlfcn.h>
namespace anyxbox {
int32_t sdl_keycode_to_vk(int32_t s) {
  if (s >= 'a' && s <= 'z') return s - 32;
  if (s >= '0' && s <= '9') return s;
  switch (s) {
    case ' ': return 0x20; case '\r': return 0x0D; case 27: return 0x1B; case '\b': return 0x08; case '\t': return 0x09;
    case 0x4000004F: return 0x27; case 0x40000050: return 0x25; case 0x40000051: return 0x28; case 0x40000052: return 0x26;
    case 0x400000E1: case 0x400000E5: return 0x10; case 0x400000E0: case 0x400000E4: return 0x11;
  }
  if (s >= 0x4000003A && s <= 0x40000045) return 0x70 + (s - 0x4000003A);   // F1..F12
  return 0;
}
namespace {
struct Sdl {
  void* lib = nullptr;
  int (*Init)(uint32_t) = nullptr; void (*Quit)() = nullptr;
  void* (*CreateWindow)(const char*, int, int, int, int, uint32_t) = nullptr;
  void* (*CreateRenderer)(void*, int, uint32_t) = nullptr;
  void* (*CreateTexture)(void*, uint32_t, int, int, int) = nullptr;
  int (*UpdateTexture)(void*, const void*, const void*, int) = nullptr;
  int (*RenderCopy)(void*, void*, const void*, const void*) = nullptr;
  void (*RenderPresent)(void*) = nullptr;
  void (*DestroyTexture)(void*) = nullptr; void (*DestroyRenderer)(void*) = nullptr; void (*DestroyWindow)(void*) = nullptr;
  int (*PollEvent)(void*) = nullptr;
  bool load() {
    for (const char* n : {"libSDL2-2.0.so.0", "libSDL2.so"}) if ((lib = dlopen(n, RTLD_NOW | RTLD_LOCAL))) break;
    if (!lib) return false;
#define S(f) f = (decltype(f))dlsym(lib, "SDL_" #f); if (!f) return false;
    S(Init) S(Quit) S(CreateRenderer) S(CreateTexture) S(UpdateTexture) S(RenderCopy) S(RenderPresent)
    S(DestroyTexture) S(DestroyRenderer) S(DestroyWindow) S(PollEvent)
#undef S
    CreateWindow = (decltype(CreateWindow))dlsym(lib, "SDL_CreateWindow");
    return CreateWindow != nullptr;
  }
};
class SdlWindow : public HostWindow {
 public:
  ~SdlWindow() override { close(); if (s_.lib) dlclose(s_.lib); }
  bool load() { return s_.load(); }
  bool open(uint32_t w, uint32_t h, const char* title) override {
    close();
    if (!w || !h || w > 16384 || h > 16384) return false;
    if (s_.Init(0x20) != 0) return false;      // SDL_INIT_VIDEO
    inited_ = true;
    win_ = s_.CreateWindow(title ? title : "Anyxbox", 0x1FFF0000, 0x1FFF0000, (int)w, (int)h, 0);   // UNDEFINED pos
    if (!win_) { close(); return false; }
    ren_ = s_.CreateRenderer(win_, -1, 0);
    if (!ren_) ren_ = s_.CreateRenderer(win_, -1, 1);   // software
    if (!ren_) { close(); return false; }
    return make_tex(w, h);
  }
  void present(const uint8_t* rgba, uint32_t w, uint32_t h) override {
    if (!ren_ || !rgba || !w || !h) return;
    if (w != tw_ || h != th_) if (!make_tex(w, h)) return;
    s_.UpdateTexture(tex_, nullptr, rgba, (int)(w * 4));
    s_.RenderCopy(ren_, tex_, nullptr, nullptr);
    s_.RenderPresent(ren_);
  }
  bool poll(HostEvent& ev) override {
    if (!win_) return false;
    alignas(8) uint8_t buf[64] = {};
    while (s_.PollEvent(buf)) {
      uint32_t t; std::memcpy(&t, buf, 4);
      auto i32 = [&](int o) { int32_t v; std::memcpy(&v, buf + o, 4); return v; };
      ev = HostEvent{};
      switch (t) {
        case 0x100: ev.type = HostEvent::Quit; return true;
        case 0x300: case 0x301: {
          int32_t vk = sdl_keycode_to_vk(i32(20));
          if (!vk) continue;
          ev.type = t == 0x300 ? HostEvent::KeyDown : HostEvent::KeyUp; ev.key = vk; return true;
        }
        case 0x400: ev.type = HostEvent::MouseMove; ev.x = i32(20); ev.y = i32(24); return true;
        case 0x401: case 0x402: {
          ev.type = t == 0x401 ? HostEvent::MouseDown : HostEvent::MouseUp;
          ev.button = buf[16]; ev.x = i32(20); ev.y = i32(24); return true;
        }
        default: continue;
      }
    }
    return false;
  }
  void close() override {
    if (tex_) s_.DestroyTexture(tex_);
    if (ren_) s_.DestroyRenderer(ren_);
    if (win_) s_.DestroyWindow(win_);
    tex_ = ren_ = win_ = nullptr; tw_ = th_ = 0;
    if (inited_) { s_.Quit(); inited_ = false; }
  }
 private:
  bool make_tex(uint32_t w, uint32_t h) {
    if (tex_) { s_.DestroyTexture(tex_); tex_ = nullptr; }
    tex_ = s_.CreateTexture(ren_, 0x16762004u, 1, (int)w, (int)h);   // ABGR8888 == RGBA bytes, streaming
    if (!tex_) return false;
    tw_ = w; th_ = h; return true;
  }
  Sdl s_; void* win_ = nullptr; void* ren_ = nullptr; void* tex_ = nullptr;
  uint32_t tw_ = 0, th_ = 0; bool inited_ = false;
};
}
std::unique_ptr<HostWindow> make_sdl_window() {
  auto w = std::make_unique<SdlWindow>();
  if (!w->load()) return nullptr;
  return w;
}
}
#else
namespace anyxbox {
int32_t sdl_keycode_to_vk(int32_t) { return 0; }
std::unique_ptr<HostWindow> make_sdl_window() { return nullptr; }
}
#endif
