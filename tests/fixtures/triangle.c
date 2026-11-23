/* Anyxbox fixture: a self-written D3D11 triangle for a freestanding Windows x64 program (no C runtime, no
 * Microsoft headers). Build with MinGW (see tests/fixtures/README.md and .github/workflows/fixture.yml).
 * The same file is compiled into test_fixture_source with ANYXBOX_HOST_TEST, where the imports are function
 * pointers bound to Anyxbox's own host functions, so its logic is exercised without a Windows toolchain.
 * Shaders are the precompiled DXBC in triangle_shaders.h; nothing is compiled at run time.
 * Exit code: 0 ok, otherwise the number of the failing step. Runs until the window is closed or FRAMES frames. */
#include <stdint.h>
#include "triangle_shaders.h"
#ifndef FRAMES
#define FRAMES 3
#endif
#define MS __attribute__((ms_abi))
#ifdef ANYXBOX_HOST_TEST
extern "C" void anyxbox_fixture_import(const char* dll, const char* name, void** slot);
#define IMPORT(ret, dll, name, args) static ret (MS *name) args; \
  static void __attribute__((constructor)) reg_##name(void) { anyxbox_fixture_import(dll, #name, (void**)&name); }
#else
#define IMPORT(ret, dll, name, args) __declspec(dllimport) ret MS name args;
#endif
#ifdef ANYXBOX_HOST_TEST
extern "C" {
#endif
typedef struct { uint64_t hwnd; uint32_t message; uint64_t wParam, lParam; uint32_t time; int32_t x, y; } MSGX __attribute__((aligned(8)));
IMPORT(uint32_t, "user32.dll", RegisterClassExA, (const void*))
IMPORT(uint64_t, "user32.dll", CreateWindowExA, (uint32_t, const char*, const char*, uint32_t, int, int, int, int, uint64_t, uint64_t, uint64_t, void*))
IMPORT(uint32_t, "user32.dll", ShowWindow, (uint64_t, int))
IMPORT(uint32_t, "user32.dll", PeekMessageA, (void*, uint64_t, uint32_t, uint32_t, uint32_t))
IMPORT(int64_t, "user32.dll", DispatchMessageA, (const void*))
IMPORT(int64_t, "user32.dll", DefWindowProcA, (uint64_t, uint32_t, uint64_t, uint64_t))
IMPORT(uint32_t, "user32.dll", DestroyWindow, (uint64_t))
IMPORT(void, "user32.dll", PostQuitMessage, (int))
IMPORT(void, "kernel32.dll", ExitProcess, (uint32_t))
IMPORT(int32_t, "d3d11.dll", D3D11CreateDeviceAndSwapChain, (void*, uint32_t, void*, uint32_t, const void*, uint32_t, uint32_t, const void*, void**, void**, uint32_t*, void**))

#define VT(o) (*(void***)(o))
typedef int32_t (MS *F_Present)(void*, uint32_t, uint32_t);
typedef int32_t (MS *F_GetBuffer)(void*, uint32_t, const void*, void**);
typedef int32_t (MS *F_CreateBuffer)(void*, const void*, const void*, void**);
typedef int32_t (MS *F_CreateRTV)(void*, void*, const void*, void**);
typedef int32_t (MS *F_CreateLayout)(void*, const void*, uint32_t, const void*, uint64_t, void**);
typedef int32_t (MS *F_CreateShader)(void*, const void*, uint64_t, void*, void**);
typedef void (MS *F_OMSetRT)(void*, uint32_t, void* const*, void*);
typedef void (MS *F_RSSetVP)(void*, uint32_t, const void*);
typedef void (MS *F_ClearRTV)(void*, void*, const float*);
typedef void (MS *F_SetLayout)(void*, void*);
typedef void (MS *F_SetVB)(void*, uint32_t, uint32_t, void* const*, const uint32_t*, const uint32_t*);
typedef void (MS *F_SetTopo)(void*, uint32_t);
typedef void (MS *F_SetShader)(void*, void*, void* const*, uint32_t);
typedef void (MS *F_Draw)(void*, uint32_t, uint32_t);

static void zero(void* p, int n) { volatile uint8_t* b = (volatile uint8_t*)p; while (n-- > 0) *b++ = 0; }
static void put32(void* p, int off, uint32_t v) { *(volatile uint32_t*)((uint8_t*)p + off) = v; }
static void put64(void* p, int off, uint64_t v) { *(volatile uint64_t*)((uint8_t*)p + off) = v; }

static MS int64_t wnd_proc(uint64_t h, uint32_t m, uint64_t w, uint64_t l) {
  if (m == 0x10) { DestroyWindow(h); return 0; }       /* WM_CLOSE */
  if (m == 2) { PostQuitMessage(0); return 0; }          /* WM_DESTROY */
  return DefWindowProcA(h, m, w, l);
}

static const char kClass[] = "AnyxboxTriangle";
static const char kPos[] = "POSITION", kCol[] = "COLOR";
/* x y z w r g b a, clockwise in D3D clip space (y up) */
static const float kVerts[3][8] = {
  { 0.0f,  0.6f, 0.5f, 1.0f, 1, 0, 0, 1 },
  { 0.6f, -0.6f, 0.5f, 1.0f, 0, 1, 0, 1 },
  {-0.6f, -0.6f, 0.5f, 1.0f, 0, 0, 1, 1 },
};
static const uint8_t kIidTexture2D[16] = {0xf2,0xaa,0x15,0x6f,0x08,0xd2,0x89,0x4e,0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c};

MS uint32_t entry(void) {
  uint8_t wc[80]; zero(wc, sizeof wc);
  put32(wc, 0, 80); put64(wc, 8, (uint64_t)(uintptr_t)&wnd_proc); put64(wc, 64, (uint64_t)(uintptr_t)kClass);
  if (!RegisterClassExA(wc)) return 1;
  uint64_t hwnd = CreateWindowExA(0, kClass, "Anyxbox triangle", 0x00CF0000, 100, 100, 320, 240, 0, 0, 0, 0);
  if (!hwnd) return 2;
  ShowWindow(hwnd, 5);

  uint8_t sd[72]; zero(sd, sizeof sd);                 /* DXGI_SWAP_CHAIN_DESC */
  put32(sd, 0, 320); put32(sd, 4, 240); put32(sd, 16, 28 /* R8G8B8A8_UNORM */); put32(sd, 28, 1);
  put32(sd, 36, 0x20); put32(sd, 40, 1); put64(sd, 48, hwnd); put32(sd, 56, 1);
  void *swap = 0, *dev = 0, *ctx = 0; uint32_t level = 0;
  if (D3D11CreateDeviceAndSwapChain(0, 1, 0, 0, 0, 0, 7, sd, &swap, &dev, &level, &ctx) < 0) return 3;
  void *bb = 0, *rtv = 0;
  if (((F_GetBuffer)VT(swap)[9])(swap, 0, kIidTexture2D, &bb) < 0) return 4;
  if (((F_CreateRTV)VT(dev)[9])(dev, bb, 0, &rtv) < 0) return 5;

  uint8_t bd[24]; zero(bd, sizeof bd); put32(bd, 0, sizeof kVerts); put32(bd, 8, 1 /* VERTEX_BUFFER */);
  uint8_t sub[16]; zero(sub, sizeof sub); put64(sub, 0, (uint64_t)(uintptr_t)kVerts);
  void* vb = 0;
  if (((F_CreateBuffer)VT(dev)[3])(dev, bd, sub, &vb) < 0) return 6;

  uint8_t el[64]; zero(el, sizeof el);                   /* two D3D11_INPUT_ELEMENT_DESC, 32 bytes each */
  put64(el, 0, (uint64_t)(uintptr_t)kPos); put32(el, 12, 2 /* R32G32B32A32_FLOAT */); put32(el, 20, 0);
  put64(el, 32, (uint64_t)(uintptr_t)kCol); put32(el, 32 + 12, 2); put32(el, 32 + 20, 16);
  void *layout = 0, *vs = 0, *ps = 0;
  if (((F_CreateLayout)VT(dev)[11])(dev, el, 2, triangle_vs, sizeof triangle_vs, &layout) < 0) return 7;
  if (((F_CreateShader)VT(dev)[12])(dev, triangle_vs, sizeof triangle_vs, 0, &vs) < 0) return 8;
  if (((F_CreateShader)VT(dev)[15])(dev, triangle_ps, sizeof triangle_ps, 0, &ps) < 0) return 9;

  const float vp[6] = {0, 0, 320, 240, 0, 1};
  const float clear[4] = {0.05f, 0.05f, 0.15f, 1.0f};
  const uint32_t stride = 32, offset = 0;
  int frames = 0, quit = 0;
  while (!quit && frames < FRAMES) {
    MSGX msg;
    while (PeekMessageA(&msg, 0, 0, 0, 1)) {
      if (msg.message == 0x12) { quit = 1; break; }       /* WM_QUIT */
      DispatchMessageA(&msg);
    }
    if (quit) break;
    ((F_OMSetRT)VT(ctx)[33])(ctx, 1, &rtv, 0);
    ((F_RSSetVP)VT(ctx)[44])(ctx, 1, vp);
    ((F_ClearRTV)VT(ctx)[50])(ctx, rtv, clear);
    ((F_SetLayout)VT(ctx)[17])(ctx, layout);
    ((F_SetVB)VT(ctx)[18])(ctx, 0, 1, &vb, &stride, &offset);
    ((F_SetTopo)VT(ctx)[24])(ctx, 4 /* TRIANGLELIST */);
    ((F_SetShader)VT(ctx)[11])(ctx, vs, 0, 0);
    ((F_SetShader)VT(ctx)[9])(ctx, ps, 0, 0);
    ((F_Draw)VT(ctx)[13])(ctx, 3, 0);
    if (((F_Present)VT(swap)[8])(swap, 0, 0) < 0) return 10;
    frames++;
  }
  return frames == 0 && !quit ? 11 : 0;
}
#ifdef ANYXBOX_HOST_TEST
}
#else
void MS mainEntry(void) { ExitProcess(entry()); }
#endif
