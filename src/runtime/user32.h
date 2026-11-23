// Anyxbox user32 subset: windows, message queue, window procedures. GPL-2.0.
// Windows x64 calling convention, Linux x86-64 hosts only. Guest window procedures are called natively.
#pragma once
#include <cstdint>
#include <array>
#include <deque>
#include <map>
#include <string>
#include "runtime/host_window.h"
#include "syslayer/import_registry.h"
namespace anyxbox {
struct GuestWindow {
  uint64_t wndproc = 0; std::string cls, title; int x = 0, y = 0, w = 0, h = 0; bool visible = false; bool created_host = false;
  uint64_t userdata = 0;
};
struct GuestMsg { uint64_t hwnd = 0; uint32_t msg = 0; uint64_t wparam = 0, lparam = 0; int32_t px = 0, py = 0; };
struct User32Context {
  HostWindow* host = nullptr;                 // optional; null = headless (no display, input comes only from PostMessage)
  std::map<std::string, uint64_t> classes;    // class name -> wndproc
  std::map<uint64_t, GuestWindow> windows;    // HWND -> window
  std::deque<GuestMsg> queue;
  uint64_t next_hwnd = 0x10010;
  bool quit_posted = false; int quit_code = 0;
  int dispatched = 0;
  std::string last_message_box;               // MessageBox text is recorded, never shown
  std::array<bool,256> keys_down{}; // current translated host key state, high-bit GetAsyncKeyState subset
  void pump_host();                           // moves host events into the queue
};
// Registers RegisterClassA/ExA, CreateWindowExA, DestroyWindow, ShowWindow, UpdateWindow, PeekMessageA, GetMessageA,
// TranslateMessage, DispatchMessageA, DefWindowProcA, PostQuitMessage, PostMessageA, SendMessageA, GetClientRect,
// AdjustWindowRect, SetWindowTextA, LoadCursorA, LoadIconA, MessageBoxA, GetAsyncKeyState, IsWindow, SetWindowLongPtrA stubs.
// The context must outlive every call. Returns false on hosts that cannot run Windows-ABI code.
bool user32_register(ImportRegistry& reg, User32Context& ctx);
}
