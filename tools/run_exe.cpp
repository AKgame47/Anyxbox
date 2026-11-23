// run_exe --i-built-this <file.exe> [--window]: run a self-built Windows x64 program through Anyxbox.
// Prints unresolved imports, the exit code, captured output and the number of frames presented.
// --window opens an SDL2 window when libSDL2 and a display exist; otherwise the run is headless.
// WARNING: the program's machine code runs natively with your privileges. Use only binaries you built yourself.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "runtime/run_pe.h"
#include "runtime/user32.h"
#include "runtime/window_present.h"
int main(int argc, char** argv) {
  bool trust = false, window = false; const char* path = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--i-built-this")) trust = true;
    else if (!std::strcmp(argv[i], "--window")) window = true;
    else path = argv[i];
  }
  if (!path || !trust) { std::puts("usage: run_exe --i-built-this <file.exe> [--window]"); return 2; }
  std::ifstream f(path, std::ios::binary);
  if (!f) { std::puts("cannot open file"); return 2; }
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
  anyxbox::User32Context u; anyxbox::D3D11Runtime rt;
  std::unique_ptr<anyxbox::HostWindow> win;
  if (window) { win = anyxbox::make_sdl_window(); if (!win) std::puts("note: libSDL2 not found, running headless"); }
  u.host = win.get();
  anyxbox::attach_window_present(rt, u);
  auto r = anyxbox::run_pe(d.data(), d.size(), true, [&](anyxbox::ImportRegistry& reg) { anyxbox::user32_register(reg, u); rt.register_imports(reg); });
  std::printf("loaded=%d ran=%d exit=%u error='%s'\n", r.loaded, r.ran, r.exit_code, r.error.c_str());
  for (auto& s : r.unresolved) std::printf("  missing import: %s\n", s.c_str());
  if (!r.output.empty()) std::printf("output: %s\n", r.output.c_str());
  std::printf("frames presented=%d d3d11 last_error='%s' shaders_translated=%d\n", rt.presents, rt.last_error.c_str(), (int)rt.shaders_translated);
  return r.ran && r.exit_code == 0 ? 0 : 1;
}
