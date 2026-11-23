#include "runtime/user32.h"
#include <cstring>
#if defined(__linux__) && defined(__x86_64__)
#define MSABI __attribute__((ms_abi))
namespace anyxbox {
namespace {
User32Context* C = nullptr;
enum { WM_CREATE = 1, WM_DESTROY = 2, WM_SIZE = 5, WM_CLOSE = 0x10, WM_QUIT = 0x12, WM_PAINT = 0xF, WM_KEYDOWN = 0x100, WM_KEYUP = 0x101,
       WM_MOUSEMOVE = 0x200, WM_LBUTTONDOWN = 0x201, WM_LBUTTONUP = 0x202, WM_RBUTTONDOWN = 0x204, WM_RBUTTONUP = 0x205 };
struct MSG64 { uint64_t hwnd; uint32_t message; uint32_t pad0; uint64_t wParam; uint64_t lParam; uint32_t time; int32_t x, y; uint32_t pad1; };
static_assert(sizeof(MSG64) == 48, "MSG layout");
typedef int64_t (MSABI *WndProc)(uint64_t, uint32_t, uint64_t, uint64_t);
uint64_t first_window() { return C->windows.empty() ? 0 : C->windows.begin()->first; }
int64_t call_proc(uint64_t hwnd, uint32_t m, uint64_t w, uint64_t l) {
  auto it = C->windows.find(hwnd);
  if (it == C->windows.end() || !it->second.wndproc) return 0;
  return ((WndProc)it->second.wndproc)(hwnd, m, w, l);
}
void C_pump_one(const HostEvent& e) {
  uint64_t hw = first_window();
  GuestMsg g; g.hwnd = hw;
  auto pos = [&](int x, int y) { return (uint64_t)(((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF)); };
  switch (e.type) {
    case HostEvent::Quit: g.msg = WM_CLOSE; break;
    case HostEvent::KeyDown: g.msg = WM_KEYDOWN; g.wparam = (uint64_t)e.key; C->last_key_down = (uint32_t)e.key; break;
    case HostEvent::KeyUp: g.msg = WM_KEYUP; g.wparam = (uint64_t)e.key; break;
    case HostEvent::MouseMove: g.msg = WM_MOUSEMOVE; g.lparam = pos(e.x, e.y); g.px = e.x; g.py = e.y; break;
    case HostEvent::MouseDown: g.msg = e.button == 2 ? WM_RBUTTONDOWN : WM_LBUTTONDOWN; g.lparam = pos(e.x, e.y); g.px = e.x; g.py = e.y; break;
    case HostEvent::MouseUp: g.msg = e.button == 2 ? WM_RBUTTONUP : WM_LBUTTONUP; g.lparam = pos(e.x, e.y); g.px = e.x; g.py = e.y; break;
    default: return;
  }
  C->queue.push_back(g);
}
bool next_msg(MSG64* out, bool remove) {
  C->pump_host();
  if (C->queue.empty()) {
    if (!C->quit_posted) return false;
    if (remove) C->quit_posted = false;
    std::memset(out, 0, sizeof *out); out->message = WM_QUIT; out->wParam = (uint64_t)C->quit_code; return true;
  }
  GuestMsg g = C->queue.front(); if (remove) C->queue.pop_front();
  std::memset(out, 0, sizeof *out);
  out->hwnd = g.hwnd; out->message = g.msg; out->wParam = g.wparam; out->lParam = g.lparam; out->x = g.px; out->y = g.py;
  return true;
}
const char* nz(const char* s) { return s ? s : ""; }

MSABI uint32_t u_RegisterClassExA(const uint8_t* wc) {
  if (!wc) return 0;
  uint32_t cb; std::memcpy(&cb, wc, 4);
  if (cb != 80) return 0;
  uint64_t proc, name; std::memcpy(&proc, wc + 8, 8); std::memcpy(&name, wc + 64, 8);
  if (!proc || !name || name < 0x10000) return 0;       // atom names are not supported
  std::string n((const char*)name);
  if (n.empty() || n.size() > 256 || C->classes.count(n)) return 0;
  C->classes[n] = proc;
  return (uint32_t)(0xC000 + C->classes.size());
}
MSABI uint32_t u_RegisterClassA(const uint8_t* wc) {   // WNDCLASSA: style 0, lpfnWndProc 8, ... lpszClassName 64? (x64: 72 bytes, name at 64)
  if (!wc) return 0;
  uint64_t proc, name; std::memcpy(&proc, wc + 8, 8); std::memcpy(&name, wc + 64, 8);
  if (!proc || !name || name < 0x10000) return 0;
  std::string n((const char*)name);
  if (n.empty() || n.size() > 256 || C->classes.count(n)) return 0;
  C->classes[n] = proc;
  return (uint32_t)(0xC000 + C->classes.size());
}
MSABI uint64_t u_CreateWindowExA(uint32_t, const char* cls, const char* title, uint32_t, int x, int y, int w, int h,
                                 uint64_t, uint64_t, uint64_t, void*) {
  if (!cls || (uint64_t)cls < 0x10000) return 0;
  auto ci = C->classes.find(cls);
  if (ci == C->classes.end()) return 0;
  if (C->windows.size() >= 16) return 0;
  const int kDef = (int)0x80000000;
  if (w == kDef) w = 640;
  if (h == kDef) h = 480;
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return 0;
  uint64_t id = C->next_hwnd; C->next_hwnd += 0x10;
  GuestWindow g; g.wndproc = ci->second; g.cls = cls; g.title = nz(title); g.x = x == kDef ? 0 : x; g.y = y == kDef ? 0 : y; g.w = w; g.h = h;
  C->windows[id] = g;
  uint8_t cs[64] = {};
  if (call_proc(id, WM_CREATE, 0, (uint64_t)(uintptr_t)cs) == -1) { C->windows.erase(id); return 0; }
  return id;
}
MSABI uint32_t u_DestroyWindow(uint64_t h) {
  if (!C->windows.count(h)) return 0;
  call_proc(h, WM_DESTROY, 0, 0);
  C->windows.erase(h);
  if (C->windows.empty() && C->host) C->host->close();
  return 1;
}
MSABI uint32_t u_ShowWindow(uint64_t h, int cmd) {
  auto it = C->windows.find(h); if (it == C->windows.end()) return 0;
  bool was = it->second.visible; it->second.visible = cmd != 0;
  if (it->second.visible && !it->second.created_host && C->host) {
    it->second.created_host = C->host->open((uint32_t)it->second.w, (uint32_t)it->second.h, it->second.title.c_str());
  }
  if (it->second.visible) { GuestMsg g; g.hwnd = h; g.msg = WM_PAINT; C->queue.push_back(g); }
  return was ? 1 : 0;
}
MSABI uint32_t u_UpdateWindow(uint64_t h) { return C->windows.count(h) ? 1 : 0; }
MSABI uint32_t u_PeekMessageA(MSG64* m, uint64_t, uint32_t, uint32_t, uint32_t remove) {
  if (!m) return 0;
  return next_msg(m, (remove & 1) != 0) ? 1 : 0;
}
MSABI int32_t u_GetMessageA(MSG64* m, uint64_t, uint32_t, uint32_t) {
  if (!m) return -1;
  if (!next_msg(m, true)) return 0;     // nothing can ever arrive without an event source: act as if quit (no deadlock)
  return m->message == WM_QUIT ? 0 : 1;
}
MSABI uint32_t u_TranslateMessage(const MSG64*) { return 0; }
MSABI int64_t u_DispatchMessageA(const MSG64* m) {
  if (!m) return 0;
  C->dispatched++;
  return call_proc(m->hwnd, m->message, m->wParam, m->lParam);
}
MSABI int64_t u_DefWindowProcA(uint64_t h, uint32_t m, uint64_t, uint64_t) {
  if (m == WM_CLOSE) { u_DestroyWindow(h); return 0; }
  return 0;
}
MSABI void u_PostQuitMessage(int code) { C->quit_posted = true; C->quit_code = code; }
MSABI uint32_t u_PostMessageA(uint64_t h, uint32_t m, uint64_t w, uint64_t l) {
  if (h && !C->windows.count(h)) return 0;
  GuestMsg g; g.hwnd = h; g.msg = m; g.wparam = w; g.lparam = l; C->queue.push_back(g); return 1;
}
MSABI int64_t u_SendMessageA(uint64_t h, uint32_t m, uint64_t w, uint64_t l) { return call_proc(h, m, w, l); }
MSABI uint32_t u_GetClientRect(uint64_t h, int32_t* r) {
  auto it = C->windows.find(h); if (it == C->windows.end() || !r) return 0;
  r[0] = 0; r[1] = 0; r[2] = it->second.w; r[3] = it->second.h; return 1;
}
MSABI uint32_t u_AdjustWindowRect(int32_t*, uint32_t, uint32_t) { return 1; }   // no frame is drawn: the client rect is the window
MSABI uint32_t u_SetWindowTextA(uint64_t h, const char* t) {
  auto it = C->windows.find(h); if (it == C->windows.end()) return 0;
  it->second.title = nz(t); return 1;
}
MSABI uint64_t u_LoadCursorA(uint64_t, const char*) { return 0x7001; }
MSABI uint64_t u_LoadIconA(uint64_t, const char*) { return 0x7002; }
MSABI int32_t u_MessageBoxA(uint64_t, const char* text, const char* cap, uint32_t) {
  C->last_message_box = std::string(nz(cap)) + ": " + nz(text); return 1;
}
MSABI uint16_t u_GetAsyncKeyState(int vk) { return (uint32_t)vk == C->last_key_down && vk ? 0x8000 : 0; }
MSABI uint32_t u_IsWindow(uint64_t h) { return C->windows.count(h) ? 1 : 0; }
MSABI int64_t u_SetWindowLongPtrA(uint64_t h, int idx, int64_t v) {
  auto it = C->windows.find(h); if (it == C->windows.end() || idx != -21) return 0;     // GWLP_USERDATA only
  int64_t old = (int64_t)it->second.userdata; it->second.userdata = (uint64_t)v; return old;
}
MSABI int64_t u_GetWindowLongPtrA(uint64_t h, int idx) {
  auto it = C->windows.find(h); if (it == C->windows.end() || idx != -21) return 0;
  return (int64_t)it->second.userdata;
}

// ---- wide-character entry points: text is narrowed (ASCII only, other characters become '?') and the A function is used ----
std::string narrow(const uint16_t* w) {
  std::string o; if (!w) return o;
  for (size_t i = 0; w[i] && i < 1024; i++) o.push_back(w[i] < 0x80 ? (char)w[i] : '?');
  return o;
}
MSABI uint32_t u_RegisterClassExW(const uint8_t* wc) {
  if (!wc) return 0;
  uint8_t c[80]; std::memcpy(c, wc, 80);
  uint64_t name; std::memcpy(&name, wc + 64, 8);
  if (name < 0x10000) return 0;
  static thread_local std::string keep; keep = narrow((const uint16_t*)name);
  uint64_t np = (uint64_t)(uintptr_t)keep.c_str(); std::memcpy(c + 64, &np, 8);
  return u_RegisterClassExA(c);
}
MSABI uint64_t u_CreateWindowExW(uint32_t ex, const uint16_t* cls, const uint16_t* title, uint32_t st, int x, int y, int w, int h,
                                 uint64_t p, uint64_t m, uint64_t i, void* par) {
  if (!cls || (uint64_t)(uintptr_t)cls < 0x10000) return 0;
  std::string c = narrow(cls), t = narrow(title);
  return u_CreateWindowExA(ex, c.c_str(), t.c_str(), st, x, y, w, h, p, m, i, par);
}
MSABI int64_t u_DefWindowProcW(uint64_t h, uint32_t m, uint64_t w, uint64_t l) { return u_DefWindowProcA(h, m, w, l); }
MSABI uint32_t u_PeekMessageW(MSG64* m, uint64_t h, uint32_t a, uint32_t b, uint32_t r) { return u_PeekMessageA(m, h, a, b, r); }
MSABI int32_t u_GetMessageW(MSG64* m, uint64_t h, uint32_t a, uint32_t b) { return u_GetMessageA(m, h, a, b); }
MSABI int64_t u_DispatchMessageW(const MSG64* m) { return u_DispatchMessageA(m); }
MSABI uint32_t u_PostMessageW(uint64_t h, uint32_t m, uint64_t w, uint64_t l) { return u_PostMessageA(h, m, w, l); }
MSABI int64_t u_SendMessageW(uint64_t h, uint32_t m, uint64_t w, uint64_t l) { return u_SendMessageA(h, m, w, l); }
MSABI uint32_t u_SetWindowTextW(uint64_t h, const uint16_t* t) { std::string s = narrow(t); return u_SetWindowTextA(h, s.c_str()); }
MSABI uint64_t u_LoadCursorW(uint64_t, const uint16_t*) { return 0x7001; }
MSABI uint64_t u_LoadIconW(uint64_t, const uint16_t*) { return 0x7002; }
MSABI int32_t u_MessageBoxW(uint64_t h, const uint16_t* t, const uint16_t* c, uint32_t f) { std::string a = narrow(t), b = narrow(c); return u_MessageBoxA(h, a.c_str(), b.c_str(), f); }
MSABI int64_t u_SetWindowLongPtrW(uint64_t h, int i, int64_t v) { return u_SetWindowLongPtrA(h, i, v); }
MSABI int64_t u_GetWindowLongPtrW(uint64_t h, int i) { return u_GetWindowLongPtrA(h, i); }
}
void User32Context::pump_host() {
  if (!host) return;
  HostEvent e;
  while (host->poll(e)) C_pump_one(e);
}
bool user32_register(ImportRegistry& reg, User32Context& ctx) {
  C = &ctx;
#define A(n) reg.add("user32.dll", #n, (uint64_t)(uintptr_t)&u_##n)
  A(RegisterClassExA); A(RegisterClassA); A(CreateWindowExA); A(DestroyWindow); A(ShowWindow); A(UpdateWindow);
  A(PeekMessageA); A(GetMessageA); A(TranslateMessage); A(DispatchMessageA); A(DefWindowProcA); A(PostQuitMessage);
  A(PostMessageA); A(SendMessageA); A(GetClientRect); A(AdjustWindowRect); A(SetWindowTextA); A(LoadCursorA);
  A(RegisterClassExW); A(CreateWindowExW); A(DefWindowProcW); A(PeekMessageW); A(GetMessageW); A(DispatchMessageW);
  A(PostMessageW); A(SendMessageW); A(SetWindowTextW); A(LoadCursorW); A(LoadIconW); A(MessageBoxW); A(SetWindowLongPtrW); A(GetWindowLongPtrW);
  A(LoadIconA); A(MessageBoxA); A(GetAsyncKeyState); A(IsWindow); A(SetWindowLongPtrA); A(GetWindowLongPtrA);
#undef A
  return true;
}
}
#else
namespace anyxbox {
void User32Context::pump_host() {}
bool user32_register(ImportRegistry&, User32Context&) { return false; }
}
#endif
