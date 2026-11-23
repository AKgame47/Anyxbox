// Anyxbox host window: a small display/input backend. The SDL2 backend loads libSDL2 with dlopen, so no SDL
// headers or link flags are needed; without SDL or a display, open() fails and callers stay headless. GPL-2.0.
#pragma once
#include <cstdint>
#include <memory>
namespace anyxbox {
struct HostEvent {
  enum Type { None, Quit, KeyDown, KeyUp, MouseMove, MouseDown, MouseUp, Resize, FocusLost } type = None;
  int32_t key = 0;        // Windows virtual-key code for keys
  int32_t x = 0, y = 0;   // mouse position, or new size for Resize
  int32_t button = 0;     // 1 left, 2 right, 3 middle
};
class HostWindow {
 public:
  virtual ~HostWindow() = default;
  virtual bool open(uint32_t w, uint32_t h, const char* title) = 0;
  virtual void present(const uint8_t* rgba, uint32_t w, uint32_t h) = 0;   // RGBA8, row 0 at top
  virtual bool poll(HostEvent& ev) = 0;                                    // false when the queue is empty
  virtual void close() = 0;
};
// Returns an SDL2 window, or null when libSDL2 cannot be loaded. open() may still fail (no display).
std::unique_ptr<HostWindow> make_sdl_window();
// Maps an SDL keycode to a Windows virtual-key code (0 when unmapped). Exposed for tests.
int32_t sdl_keycode_to_vk(int32_t sym);
}
