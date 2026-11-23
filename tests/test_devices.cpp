// Anyxbox: XInput pads and the XAudio2 engine that plays into nothing, called through the same entry points and vtables a guest uses.
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include "runtime/devices.h"
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
#define MS __attribute__((ms_abi))
using namespace anyxbox;
typedef uint32_t (MS *GetStateFn)(uint32_t, uint8_t*);
typedef uint32_t (MS *SetStateFn)(uint32_t, const uint8_t*);
typedef uint32_t (MS *CapsFn)(uint32_t, uint32_t, uint8_t*);
typedef uint32_t (MS *CreateFn)(void**, uint32_t, uint32_t);
template <class F> static F slot(void* obj, int i) { return (F)(*(void***)obj)[i]; }
int main() {
  ImportRegistry reg; XInputContext xi; XAudio2Context xa;
  CHECK(xinput_register(reg, xi) && xaudio2_register(reg, xa));
  CHECK(reg.has_dll("XInput1_4.dll") && reg.has_dll("xinput1_3.dll") && reg.has_dll("xinput9_1_0.dll") && reg.has_dll("xaudio2_9.dll"));
  auto gs = (GetStateFn)(uintptr_t)reg.resolve("xinput1_4.dll", "XInputGetState", 0, false);
  auto ss = (SetStateFn)(uintptr_t)reg.resolve("xinput1_4.dll", "XInputSetState", 0, false);
  auto gc = (CapsFn)(uintptr_t)reg.resolve("xinput1_3.dll", "XInputGetCapabilities", 0, false);
  CHECK(gs && ss && gc);
  uint8_t st[16]; std::memset(st, 0xAA, sizeof st);
  CHECK(gs(0, st) == 1167 && gs(7, st) == 1167 && gs(0, nullptr) == 160);          // no pad connected
  CHECK(st[0] == 0xAA);                                                              // untouched on failure
  xi.set_pad(0, true, 0x1000 /* A */, 200, 10, 12000, -5000, 1, -1);
  CHECK(gs(0, st) == 0);
  uint32_t packet; int16_t lx, ly, rx, ry; uint16_t btn; std::memcpy(&packet, st, 4); std::memcpy(&btn, st + 4, 2);
  std::memcpy(&lx, st + 8, 2); std::memcpy(&ly, st + 10, 2); std::memcpy(&rx, st + 12, 2); std::memcpy(&ry, st + 14, 2);
  CHECK(packet == 1 && btn == 0x1000 && st[6] == 200 && st[7] == 10 && lx == 12000 && ly == -5000 && rx == 1 && ry == -1);
  xi.set_pad(0, true, 0, 0, 0); gs(0, st); std::memcpy(&packet, st, 4); CHECK(packet == 2);   // packet number moves with every change
  uint8_t vib[4] = {0x34, 0x12, 0x78, 0x56};
  CHECK(ss(0, vib) == 0 && xi.pads[0].rumble_left == 0x1234 && xi.pads[0].rumble_right == 0x5678 && ss(1, vib) == 1167);
  uint8_t caps[20]; CHECK(gc(0, 0, caps) == 0 && caps[0] == 1 && gc(2, 0, caps) == 1167);
  auto enable = (void (MS *)(int32_t))reg.resolve("xinput1_4.dll", "XInputEnable", 0, false);
  enable(0); CHECK(gs(0,st) == 0); std::memcpy(&btn,st+4,2); CHECK(btn == 0 && st[6] == 0);
  enable(1);
  // SDL2 virtual-controller integration. No physical hardware or SDL build headers required.
  void* lib = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
  auto host = make_sdl_controllers();
  if (lib && host) {
    auto attach = (int (*)(int,int,int,int))dlsym(lib, "SDL_JoystickAttachVirtual");
    auto detach = (int (*)(int))dlsym(lib, "SDL_JoystickDetachVirtual");
    auto open = (void* (*)(int))dlsym(lib, "SDL_JoystickOpen");
    auto close = (void (*)(void*))dlsym(lib, "SDL_JoystickClose");
    auto axis = (int (*)(void*,int,int16_t))dlsym(lib, "SDL_JoystickSetVirtualAxis");
    auto button = (int (*)(void*,int,uint8_t))dlsym(lib, "SDL_JoystickSetVirtualButton");
    if (attach && detach && open && close && axis && button) {
      int idx = attach(1,6,15,0); CHECK(idx >= 0);
      void* joy = open(idx); CHECK(joy);
      CHECK(button(joy,0,1) == 0 && button(joy,11,1) == 0);
      CHECK(axis(joy,0,12000) == 0 && axis(joy,1,-32768) == 0 && axis(joy,4,32767) == 0);
      xi.poll = [&] { host->update(xi); };
      CHECK(gs(0,st) == 0);
      CHECK(axis(joy,4,-32768) == 0); host->update(xi);
      CHECK(axis(joy,4,32767) == 0); CHECK(gs(0,st) == 0);
      std::memcpy(&btn,st+4,2); std::memcpy(&lx,st+8,2); std::memcpy(&ly,st+10,2);
      std::printf("  SDL virtual buttons=%x lx=%d ly=%d lt=%u\n",btn,lx,ly,st[6]);
      CHECK((btn & 0x1001) == 0x1001 && lx == 12000 && ly == 32767 && st[6] == 255);
      std::memcpy(&packet,st,4); CHECK(gs(0,st) == 0); uint32_t unchanged; std::memcpy(&unchanged,st,4); CHECK(unchanged == packet);
      close(joy); CHECK(detach(idx) == 0); CHECK(gs(0,st) == 1167);
      xi.poll = nullptr;
      std::puts("  SDL virtual controller integration passed");
    } else std::puts("SKIP SDL virtual API unavailable (requires SDL 2.0.14+)");
  } else std::puts("SKIP SDL controller backend unavailable");
  host.reset(); if (lib) dlclose(lib);
  // XAudio2
  auto create = (CreateFn)(uintptr_t)reg.resolve("xaudio2_9.dll", "XAudio2Create", 0, false); CHECK(create);
  void* eng = nullptr; CHECK(create(&eng, 0, 1) == 0 && eng && xa.engines_created == 1 && create(nullptr, 0, 1) != 0);
  CHECK(slot<uint32_t (MS *)(void*)>(eng, 1)(eng) == 2 && slot<uint32_t (MS *)(void*)>(eng, 2)(eng) == 1);   // AddRef, Release
  void* master = nullptr;
  CHECK(slot<uint32_t (MS *)(void*, void**, uint32_t, uint32_t, uint32_t, const void*, void*, uint32_t)>(eng, 7)(eng, &master, 0, 0, 0, nullptr, nullptr, 0) == 0 && master);
  uint8_t det[16]; slot<void (MS *)(void*, uint8_t*)>(master, 0)(master, det);
  uint32_t ch, sr; std::memcpy(&ch, det + 8, 4); std::memcpy(&sr, det + 12, 4); CHECK(ch == 2 && sr == 48000);
  uint8_t fmt[18] = {}; uint16_t tag = 1, chn = 2, ba = 4, bits = 16; uint32_t rate = 44100, avg = 176400;
  std::memcpy(fmt, &tag, 2); std::memcpy(fmt + 2, &chn, 2); std::memcpy(fmt + 4, &rate, 4); std::memcpy(fmt + 8, &avg, 4); std::memcpy(fmt + 12, &ba, 2); std::memcpy(fmt + 14, &bits, 2);
  void* src = nullptr;
  auto csv = slot<uint32_t (MS *)(void*, void**, const uint8_t*, uint32_t, float, void*, void*, void*)>(eng, 5);
  CHECK(csv(eng, &src, fmt, 0, 2.0f, nullptr, nullptr, nullptr) == 0 && src && csv(eng, &src, nullptr, 0, 2.0f, nullptr, nullptr, nullptr) != 0);
  void* src1 = nullptr; CHECK(csv(eng, &src1, fmt, 0, 2.0f, nullptr, nullptr, nullptr) == 0);
  CHECK(slot<uint32_t (MS *)(void*, uint32_t, uint32_t)>(src1, 19)(src1, 0, 0) == 0);                   // Start
  uint8_t pcm[4000] = {}; uint8_t buf[48] = {}; uint32_t nbytes = sizeof pcm; std::memcpy(buf + 4, &nbytes, 4); const uint8_t* pd = pcm; std::memcpy(buf + 8, &pd, 8);
  auto submit = slot<uint32_t (MS *)(void*, const uint8_t*, const void*)>(src1, 21);
  CHECK(submit(src1, buf, nullptr) == 0 && submit(src1, buf, nullptr) == 0 && submit(src1, nullptr, nullptr) != 0);
  uint8_t state[24]; slot<void (MS *)(void*, uint8_t*, uint32_t)>(src1, 25)(src1, state, 0);
  uint32_t queued; uint64_t played; std::memcpy(&queued, state + 8, 4); std::memcpy(&played, state + 16, 8);
  CHECK(queued == 0 && played == 2000);                                                                  // 2 x 4000 bytes / 4 bytes per frame
  CHECK(xa.buffers_submitted == 2 && xa.bytes_submitted == 8000);
  float vol = 0; slot<uint32_t (MS *)(void*, float, uint32_t)>(src1, 12)(src1, 0.25f, 0); slot<void (MS *)(void*, float*)>(src1, 13)(src1, &vol);
  CHECK(vol == 0.25f);
  slot<void (MS *)(void*)>(src1, 18)(src1); slot<void (MS *)(void*)>(src1, 18)(src1);
  CHECK(xa.destroyed == 1 && xa.source_voices == 2 && xa.mastering_voices == 1);                         // destroying twice counts once
  CHECK(slot<uint32_t (MS *)(void*)>(eng, 8)(eng) == 0 && xa.engine_running);
  slot<void (MS *)(void*)>(eng, 9)(eng); CHECK(!xa.engine_running);
  std::puts("test_devices ok");
  return 0;
}
