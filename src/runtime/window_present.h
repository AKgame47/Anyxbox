// Anyxbox: connects the D3D11 runtime's swap chain Present to the host window. GPL-2.0.
#pragma once
#include "runtime/d3d11_runtime.h"
#include "runtime/user32.h"
namespace anyxbox {
// Every Present copies the back buffer to the host window (when one exists and is open). Both objects must outlive the guest run.
inline void attach_window_present(D3D11Runtime& rt, User32Context& u) {
  rt.on_present = [&u](const uint8_t* rgba, uint32_t w, uint32_t h) { if (u.host) u.host->present(rgba, w, h); };
}
}
