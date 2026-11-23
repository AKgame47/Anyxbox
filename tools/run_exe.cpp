// run_exe --i-built-this <file.exe> [--window] [--controller] [--gpu] [--save-frame out.png] [--asset-root DIR]: run a self-built Windows x64 program through Anyxbox.
// Prints unresolved imports, the exit code, captured output and the number of frames presented.
// --gpu draws with the Vulkan backend (a software driver such as lavapipe is enough) so translated shaders decide the pixels;
// without it the reference renderer ignores shader bytecode. Requested GPU initialization failure is fatal.
// --save-frame writes the last presented frame as a PNG; missing frames or failed writes return failure.
// --asset-root opts into read-only relative file access under an existing directory. No root means file opens fail.
// This is path routing, not isolation of native guest code.
// --window opens an SDL2 window when libSDL2 and a display exist; otherwise the run is headless.
// WARNING: the program's machine code runs natively with your privileges. Use only binaries you built yourself.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "gfx/d3d11_vulkan.h"
#include "runtime/run_pe.h"
#include "run_exe_options.h"
#include "runtime/devices.h"
#include "runtime/user32.h"
#include "runtime/window_present.h"

static uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0) {
  static uint32_t t[256]; static bool init = false;
  if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t x = i; for (int k = 0; k < 8; k++) x = x & 1 ? 0xEDB88320u ^ (x >> 1) : x >> 1; t[i] = x; } init = true; }
  c = ~c; for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 0xFF] ^ (c >> 8); return ~c;
}
static void be32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 3; i >= 0; i--) o.push_back((uint8_t)(v >> (8 * i))); }
static void chunk(std::vector<uint8_t>& o, const char* type, const std::vector<uint8_t>& body) {
  be32(o, (uint32_t)body.size());
  std::vector<uint8_t> tb(type, type + 4); tb.insert(tb.end(), body.begin(), body.end());
  o.insert(o.end(), tb.begin(), tb.end()); be32(o, crc32(tb.data(), tb.size()));
}
// RGBA8 -> PNG with stored (uncompressed) deflate blocks: no zlib needed.
static bool write_png(const char* path, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
  if (!w || !h || rgba.size() < (size_t)w * h * 4) return false;
  std::vector<uint8_t> raw; raw.reserve((size_t)(w * 4 + 1) * h);
  for (uint32_t y = 0; y < h; y++) { raw.push_back(0); raw.insert(raw.end(), rgba.begin() + (size_t)y * w * 4, rgba.begin() + (size_t)(y + 1) * w * 4); }
  std::vector<uint8_t> z = {0x78, 0x01};
  for (size_t pos = 0; pos < raw.size() || pos == 0;) {
    size_t n = std::min<size_t>(65535, raw.size() - pos); bool last = pos + n >= raw.size();
    z.push_back(last ? 1 : 0); z.push_back(n & 0xFF); z.push_back(n >> 8); z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n); pos += n; if (last) break;
  }
  uint32_t a = 1, b = 0; for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; } be32(z, (b << 16) | a);
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}, ihdr;
  be32(ihdr, w); be32(ihdr, h); ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
  chunk(png, "IHDR", ihdr); chunk(png, "IDAT", z); chunk(png, "IEND", {});
  std::ofstream f(path, std::ios::binary); if (!f) return false;
  f.write((const char*)png.data(), (std::streamsize)png.size());
  f.flush(); f.close(); return (bool)f;
}
int main(int argc, char** argv) {
  auto parsed=anyxbox::parse_launcher_options(argc,argv);
  if(!parsed.error.empty()){
    std::fprintf(stderr,"%s\n",parsed.error.c_str());
    std::puts("usage: run_exe --i-built-this [--window] [--controller] [--gpu] [--save-frame out.png] [--asset-root DIR] [--] <file.exe>");
    return 2;
  }
  auto& options=parsed.run;
  bool window=parsed.window,gpu=parsed.gpu,controller=parsed.controller;
  const char* path=parsed.path.c_str();const char* save=parsed.save.empty()?nullptr:parsed.save.c_str();
  std::ifstream f(path, std::ios::binary);
  if (!f) { std::puts("cannot open file"); return 2; }
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
  anyxbox::XInputContext xi;
  auto controllers = controller ? anyxbox::make_sdl_controllers() : nullptr;
  if (controller) {
    if (controllers) { xi.poll = [&] { controllers->update(xi); }; std::puts("controller: SDL2 input active (rumble not supported)"); }
    else std::puts("controller: SDL2 unavailable, pads disconnected");
  }
  anyxbox::User32Context u; anyxbox::D3D11Runtime rt;
  std::unique_ptr<anyxbox::HostWindow> win;
  if (window) { win = anyxbox::make_sdl_window(); if (!win) std::puts("note: libSDL2 not found, running headless"); }
  u.host = win.get();
  pnr::graphics::VulkanBackend vk; std::unique_ptr<anyxbox::d3d11::VulkanDrawBackend> be;
  if (gpu) {
    auto st = vk.open();
    if (st) st = vk.create_instance("anyxbox-run");
    if (st && !vk.devices().empty()) st = vk.create_device(0); else if (st) st = pnr::Status();
    if (st && !vk.devices().empty()) {
      be.reset(new anyxbox::d3d11::VulkanDrawBackend(vk)); rt.ctx->SetDrawBackend(be.get());
      auto* b = be.get();
      rt.on_shaders_ubo = [b](const std::vector<uint32_t>& v, const std::vector<uint32_t>& p, bool ubo) { b->SetShaders(v, p, ubo); };
      std::puts("gpu: Vulkan backend active");
    } else {
      std::fputs("gpu: requested Vulkan backend unavailable; refusing reference-renderer fallback\n",stderr);
      return 1;
    }
  }
  anyxbox::attach_window_present(rt, u);
  auto r = anyxbox::run_pe(d.data(), d.size(), true, [&](anyxbox::ImportRegistry& reg) { anyxbox::user32_register(reg, u); rt.register_imports(reg); if (controller) anyxbox::xinput_register(reg, xi); }, options);
  std::printf("loaded=%d ran=%d exit=%u error='%s'\n", r.loaded, r.ran, r.exit_code, r.error.c_str());
  for (auto& s : r.unresolved) std::printf("  missing import: %s\n", s.c_str());
  if (!r.output.empty()) std::printf("output: %s\n", r.output.c_str());
  bool saved=true;
  if(save){
    saved=write_png(save,rt.presented,rt.presented_w,rt.presented_h);
    std::printf("saved frame %s: %s\n",save,saved?"ok":"failed (no valid frame or output not writable)");
  }
  std::printf("frames presented=%d d3d11 last_error='%s' shaders_translated=%d\n", rt.presents, rt.last_error.c_str(), (int)rt.shaders_translated);
  return r.ran && r.exit_code == 0 && saved ? 0 : 1;
}
