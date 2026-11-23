// Anyxbox user32 + host window: window class, message loop, wndproc dispatch, input mapping. A fake host backend
// drives the events, so this runs headless. The SDL backend part runs only when a display exists (xvfb-run works).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "runtime/host_window.h"
#include "runtime/user32.h"
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
#define MS __attribute__((ms_abi))
using namespace anyxbox;
struct MSG64 { uint64_t hwnd; uint32_t message, pad0; uint64_t w, l; uint32_t time; int32_t x, y, pad1; };
struct Fake : HostWindow {
  std::vector<HostEvent> ev; size_t at = 0; int opens = 0, presents = 0, closes = 0; uint32_t ow = 0, oh = 0; bool ok = true;
  bool open(uint32_t w, uint32_t h, const char*) override { opens++; ow = w; oh = h; return ok; }
  void present(const uint8_t*, uint32_t, uint32_t) override { presents++; }
  bool poll(HostEvent& e) override { if (at >= ev.size()) return false; e = ev[at++]; return true; }
  void close() override { closes++; }
};
static std::vector<uint32_t> seen; static uint64_t created_hwnd = 0; static int destroyed = 0;
static MS int64_t proc(uint64_t h, uint32_t m, uint64_t w, uint64_t l);
typedef uint32_t (MS *RegEx)(const void*); typedef uint64_t (MS *CreateFn)(uint32_t, const char*, const char*, uint32_t, int, int, int, int, uint64_t, uint64_t, uint64_t, void*);
typedef uint32_t (MS *Show)(uint64_t, int); typedef uint32_t (MS *Peek)(MSG64*, uint64_t, uint32_t, uint32_t, uint32_t);
typedef int32_t (MS *Get)(MSG64*, uint64_t, uint32_t, uint32_t); typedef int64_t (MS *Disp)(const MSG64*);
typedef int64_t (MS *Def)(uint64_t, uint32_t, uint64_t, uint64_t); typedef void (MS *PQ)(int); typedef uint32_t (MS *Rect)(uint64_t, int32_t*);
typedef uint32_t (MS *Destroy)(uint64_t); typedef uint32_t (MS *Post)(uint64_t, uint32_t, uint64_t, uint64_t);
typedef int32_t (MS *MB)(uint64_t, const char*, const char*, uint32_t); typedef uint16_t (MS *Async)(int);
static ImportRegistry reg;
template <class T> static T R(const char* n) { return (T)(uintptr_t)reg.resolve("user32.dll", n, 0, false); }
static MS int64_t proc(uint64_t h, uint32_t m, uint64_t w, uint64_t l) {
  seen.push_back(m);
  if (m == 1) created_hwnd = h;
  if (m == 0x10) { R<Destroy>("DestroyWindow")(h); return 0; }
  if (m == 2) { destroyed++; R<PQ>("PostQuitMessage")(7); return 0; }
  return R<Def>("DefWindowProcA")(h, m, w, l);
}
int main() {
  Fake host; User32Context ctx; ctx.host = &host; CHECK(user32_register(reg, ctx));
  for (const char* n : {"RegisterClassExA", "RegisterClassA", "CreateWindowExA", "DestroyWindow", "ShowWindow", "PeekMessageA", "GetMessageA",
                        "DispatchMessageA", "DefWindowProcA", "PostQuitMessage", "GetClientRect", "MessageBoxA", "LoadCursorA", "GetAsyncKeyState"})
    CHECK(reg.resolve("user32.dll", n, 0, false) != 0);
  // WNDCLASSEXA (80 bytes)
  uint8_t wc[80] = {}; uint32_t cb = 80; uint64_t pr = (uint64_t)(uintptr_t)&proc; const char* cn = "AnyxboxTest"; uint64_t cnp = (uint64_t)(uintptr_t)cn;
  std::memcpy(wc, &cb, 4); std::memcpy(wc + 8, &pr, 8); std::memcpy(wc + 64, &cnp, 8);
  CHECK(R<RegEx>("RegisterClassExA")(wc) != 0);
  CHECK(R<RegEx>("RegisterClassExA")(wc) == 0);                      // duplicate class
  cb = 79; std::memcpy(wc, &cb, 4); CHECK(R<RegEx>("RegisterClassExA")(wc) == 0);   // wrong cbSize
  CHECK(R<CreateFn>("CreateWindowExA")(0, "NoSuchClass", "t", 0, 0, 0, 100, 100, 0, 0, 0, nullptr) == 0);
  CHECK(R<CreateFn>("CreateWindowExA")(0, cn, "t", 0, 0, 0, 0, 100, 0, 0, 0, nullptr) == 0);   // zero width
  uint64_t hw = R<CreateFn>("CreateWindowExA")(0, cn, "Hello", 0xCF0000, 0, 0, 320, 200, 0, 0, 0, nullptr);
  CHECK(hw && hw == created_hwnd && seen.size() == 1 && seen[0] == 1);   // WM_CREATE ran inside CreateWindow
  CHECK(host.opens == 0);                                                // no host window until ShowWindow
  CHECK(R<Show>("ShowWindow")(hw, 5) == 0 && host.opens == 1 && host.ow == 320 && host.oh == 200);
  int32_t rc4[4] = {9, 9, 9, 9}; CHECK(R<Rect>("GetClientRect")(hw, rc4) == 1 && rc4[2] == 320 && rc4[3] == 200);
  host.ev = {{HostEvent::KeyDown, 'A', 0, 0, 0}, {HostEvent::MouseMove, 0, 12, 34, 0}, {HostEvent::MouseDown, 0, 5, 6, 1}, {HostEvent::KeyUp, 0x25, 0, 0, 0}};
  CHECK(R<Async>("GetAsyncKeyState")('A') == 0x8000);
  std::vector<MSG64> got; MSG64 m;
  while (R<Peek>("PeekMessageA")(&m, 0, 0, 0, 1)) { got.push_back(m); R<Disp>("DispatchMessageA")(&m); }
  CHECK(got.size() == 5);
  CHECK(got[0].message == 0xF);                                          // WM_PAINT from ShowWindow
  CHECK(got[1].message == 0x100 && got[1].w == 'A');
  CHECK(got[2].message == 0x200 && got[2].l == ((34u << 16) | 12u) && got[2].x == 12 && got[2].y == 34);
  CHECK(got[3].message == 0x201 && got[3].hwnd == hw);
  CHECK(got[4].message == 0x101 && got[4].w == 0x25);
  CHECK(ctx.dispatched == 5);
  CHECK(R<Async>("GetAsyncKeyState")('A') == 0x8000);
  // Async polling pumps host events without requiring a guest message read.
  host.ev={{HostEvent::KeyDown,'B'},{HostEvent::KeyDown,'A'}};host.at=0;
  CHECK(R<Async>("GetAsyncKeyState")('A')==0x8000 && R<Async>("GetAsyncKeyState")('B')==0x8000);
  host.ev={{HostEvent::KeyUp,'A'}};host.at=0;
  CHECK(R<Async>("GetAsyncKeyState")('A')==0 && R<Async>("GetAsyncKeyState")('B')==0x8000);
  host.ev={{HostEvent::KeyDown,'B'},{HostEvent::KeyDown,-1},{HostEvent::KeyDown,256}};host.at=0;
  CHECK(R<Async>("GetAsyncKeyState")('B')==0x8000);
  CHECK(R<Async>("GetAsyncKeyState")(-1)==0 && R<Async>("GetAsyncKeyState")(256)==0);
  host.ev={{HostEvent::FocusLost}};host.at=0;
  CHECK(R<Async>("GetAsyncKeyState")('B')==0);
  while(R<Peek>("PeekMessageA")(&m,0,0,0,1)) {}
  std::puts("held-key checks: simultaneous keys, release, repeat, invalid codes and focus loss passed");
  // peek without remove keeps the message
  R<Post>("PostMessageA")(hw, 0x400, 1, 2);
  CHECK(R<Peek>("PeekMessageA")(&m, 0, 0, 0, 0) == 1 && m.message == 0x400);
  CHECK(R<Peek>("PeekMessageA")(&m, 0, 0, 0, 1) == 1 && m.message == 0x400);
  CHECK(R<Peek>("PeekMessageA")(&m, 0, 0, 0, 1) == 0);
  // close request -> wndproc destroys -> PostQuitMessage(7) -> GetMessage returns 0 with wParam 7
  host.ev = {{HostEvent::Quit, 0, 0, 0, 0}}; host.at = 0;
  CHECK(R<Get>("GetMessageA")(&m, 0, 0, 0) == 1 && m.message == 0x10);
  R<Disp>("DispatchMessageA")(&m);
  CHECK(destroyed == 1 && host.closes == 1 && R<Show>("ShowWindow")(hw, 5) == 0);   // window gone
  CHECK(R<Get>("GetMessageA")(&m, 0, 0, 0) == 0 && m.message == 0x12 && m.w == 7);
  CHECK(R<Get>("GetMessageA")(&m, 0, 0, 0) == 0);                        // nothing can arrive: no hang
  CHECK(R<MB>("MessageBoxA")(0, "body", "cap", 0) == 1 && ctx.last_message_box == "cap: body");
  // headless (no host): windows and messages still work
  User32Context h2; ImportRegistry reg2; CHECK(user32_register(reg2, h2));
  // note: registration re-binds the module context; use reg2's functions from here on
  auto cw = (CreateFn)(uintptr_t)reg2.resolve("user32.dll", "CreateWindowExA", 0, false);
  auto rcls = (RegEx)(uintptr_t)reg2.resolve("user32.dll", "RegisterClassExA", 0, false);
  cb = 80; std::memcpy(wc, &cb, 4); const char* cn2 = "Head"; cnp = (uint64_t)(uintptr_t)cn2; std::memcpy(wc + 64, &cnp, 8);
  CHECK(rcls(wc) != 0);
  uint64_t h3 = cw(0, cn2, "x", 0, 0, 0, 64, 64, 0, 0, 0, nullptr); CHECK(h3);
  CHECK(((Show)(uintptr_t)reg2.resolve("user32.dll", "ShowWindow", 0, false))(h3, 1) == 0);
  // wide-character variants
  { User32Context h4; ImportRegistry r4; CHECK(user32_register(r4, h4));
    auto rw = (RegEx)(uintptr_t)r4.resolve("user32.dll", "RegisterClassExW", 0, false);
    auto cwx = (CreateFn)(uintptr_t)r4.resolve("user32.dll", "CreateWindowExW", 0, false);
    static const uint16_t wname[] = {'W', 'i', 'd', 'e', 0}; static const uint16_t wtitle[] = {'T', 0xE9, 0};
    uint8_t wcw[80] = {}; uint32_t cb2 = 80; uint64_t pr2 = (uint64_t)(uintptr_t)&proc, np = (uint64_t)(uintptr_t)wname;
    std::memcpy(wcw, &cb2, 4); std::memcpy(wcw + 8, &pr2, 8); std::memcpy(wcw + 64, &np, 8);
    CHECK(rw(wcw) != 0 && rw(wcw) == 0);
    CHECK(h4.classes.count("Wide") == 1);
    uint64_t hw2 = cwx(0, (const char*)wname, (const char*)wtitle, 0, 0, 0, 50, 50, 0, 0, 0, nullptr);
    CHECK(hw2 && h4.windows[hw2].title == "T?" && h4.windows[hw2].cls == "Wide");
    typedef int32_t (MS *MBW)(uint64_t, const uint16_t*, const uint16_t*, uint32_t);
    CHECK(((MBW)(uintptr_t)r4.resolve("user32.dll", "MessageBoxW", 0, false))(0, wname, wtitle, 0) == 1 && h4.last_message_box == "T?: Wide");
    typedef int32_t (MS *GetW)(MSG64*, uint64_t, uint32_t, uint32_t);
    MSG64 mw; CHECK(((GetW)(uintptr_t)r4.resolve("user32.dll", "GetMessageW", 0, false))(&mw, 0, 0, 0) == 0);
  }
  // key mapping
  CHECK(sdl_keycode_to_vk('a') == 'A' && sdl_keycode_to_vk('7') == '7' && sdl_keycode_to_vk(0x40000050) == 0x25 && sdl_keycode_to_vk(0x4000003A) == 0x70 && sdl_keycode_to_vk(0x7FFFFFFF) == 0);
  // SDL backend: optional
  auto sdl = make_sdl_window();
  if (!sdl) std::printf("note: libSDL2 not found, SDL backend not exercised\n");
  else if (!std::getenv("DISPLAY")) { CHECK(!sdl->open(64, 64, "x")); std::printf("note: no DISPLAY, SDL open failed cleanly\n"); }
  else {
    CHECK(sdl->open(64, 48, "Anyxbox test"));
    std::vector<uint8_t> px(64 * 48 * 4, 200); sdl->present(px.data(), 64, 48);
    std::vector<uint8_t> px2(32 * 32 * 4, 50); sdl->present(px2.data(), 32, 32);   // size change recreates the texture
    HostEvent e; while (sdl->poll(e)) {}
    sdl->close(); CHECK(!sdl->poll(e));
    CHECK(sdl->open(16, 16, "again")); sdl->close();
    std::printf("SDL window opened, presented and closed on DISPLAY=%s\n", std::getenv("DISPLAY"));
  }
  std::puts("test_user32 ok");
  return 0;
}
