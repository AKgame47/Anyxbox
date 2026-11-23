/* Anyxbox fixture: a self-written freestanding D3D11 scene that uses depth testing, index buffers, per-draw constant buffer
 * updates (dynamic buffer, Map/Unmap) and a 2x2 texture with a point sampler. No C runtime, no Microsoft headers.
 * Built with MinGW in CI (see .github/workflows/fixture.yml); also compiled into test_fixture_scene with ANYXBOX_HOST_TEST.
 * Exit code: 0 ok, otherwise the number of the failing step. Runs until the window closes or FRAMES frames.
 *
 * Layout on a 320x240 target (pixel = ((x+1)*160, (1-y)*120)):
 *   near green quad (z 0.3) drawn first, far quad (z 0.7) drawn second and shifted right by a vertex constant buffer; the far quad
 *   is tinted orange by a pixel constant buffer. Where they overlap, depth keeps green. A textured quad (red, green / blue, white) sits on the right. */
#include <stdint.h>
#include "scene_shaders.h"
#ifndef FRAMES
#define FRAMES 2
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
typedef int32_t (MS *F_CreateTex)(void*, const void*, const void*, void**);
typedef int32_t (MS *F_CreateView)(void*, void*, const void*, void**);
typedef int32_t (MS *F_CreateState)(void*, const void*, void**);
typedef int32_t (MS *F_CreateLayout)(void*, const void*, uint32_t, const void*, uint64_t, void**);
typedef int32_t (MS *F_CreateShader)(void*, const void*, uint64_t, void*, void**);
typedef void (MS *F_OMSetRT)(void*, uint32_t, void* const*, void*);
typedef void (MS *F_RSSetVP)(void*, uint32_t, const void*);
typedef void (MS *F_ClearRTV)(void*, void*, const float*);
typedef void (MS *F_ClearDSV)(void*, void*, uint32_t, float, uint8_t);
typedef void (MS *F_SetLayout)(void*, void*);
typedef void (MS *F_SetVB)(void*, uint32_t, uint32_t, void* const*, const uint32_t*, const uint32_t*);
typedef void (MS *F_SetIB)(void*, void*, uint32_t, uint32_t);
typedef void (MS *F_SetTopo)(void*, uint32_t);
typedef void (MS *F_SetShader)(void*, void*, void* const*, uint32_t);
typedef void (MS *F_SetSlots)(void*, uint32_t, uint32_t, void* const*);
typedef void (MS *F_SetState)(void*, void*);
typedef void (MS *F_SetDSS)(void*, void*, uint32_t);
typedef int32_t (MS *F_Map)(void*, void*, uint32_t, uint32_t, uint32_t, void*);
typedef void (MS *F_Unmap)(void*, void*, uint32_t);
typedef void (MS *F_Update)(void*, void*, uint32_t, const void*, const void*, uint32_t, uint32_t);
typedef void (MS *F_DrawIndexed)(void*, uint32_t, uint32_t, int32_t);

static void zero(void* p, int n) { volatile uint8_t* b = (volatile uint8_t*)p; while (n-- > 0) *b++ = 0; }
static void put32(void* p, int off, uint32_t v) { *(volatile uint32_t*)((uint8_t*)p + off) = v; }
static void put64(void* p, int off, uint64_t v) { *(volatile uint64_t*)((uint8_t*)p + off) = v; }

static MS int64_t wnd_proc(uint64_t h, uint32_t m, uint64_t w, uint64_t l) {
  if (m == 0x10) { DestroyWindow(h); return 0; }
  if (m == 2) { PostQuitMessage(0); return 0; }
  return DefWindowProcA(h, m, w, l);
}

static const char kClass[] = "AnyxboxScene";
static const char kPos[] = "POSITION", kCol[] = "COLOR", kTex[] = "TEXCOORD";
static const uint8_t kIidTexture2D[16] = {0xf2,0xaa,0x15,0x6f,0x08,0xd2,0x89,0x4e,0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c};
/* x y z w  r g b a  u v  (stride 40). Quad corners: top-left, top-right, bottom-left, bottom-right. */
static const float kVerts[12][10] = {
  {-0.8f,  0.6f, 0.3f, 1, 0, 1, 0, 1, 0, 0}, {-0.1f,  0.6f, 0.3f, 1, 0, 1, 0, 1, 1, 0},     /* near, green */
  {-0.8f, -0.5f, 0.3f, 1, 0, 1, 0, 1, 0, 1}, {-0.1f, -0.5f, 0.3f, 1, 0, 1, 0, 1, 1, 1},
  {-0.9f,  0.4f, 0.7f, 1, 1, 0, 0, 1, 0, 0}, {-0.2f,  0.4f, 0.7f, 1, 1, 0, 0, 1, 1, 0},     /* far, red (+0.5 in x from the cbuffer) */
  {-0.9f, -0.6f, 0.7f, 1, 1, 0, 0, 1, 0, 1}, {-0.2f, -0.6f, 0.7f, 1, 1, 0, 0, 1, 1, 1},
  { 0.4f,  0.7f, 0.5f, 1, 1, 1, 1, 1, 0, 0}, { 0.9f,  0.7f, 0.5f, 1, 1, 1, 1, 1, 1, 0},     /* textured */
  { 0.4f, -0.7f, 0.5f, 1, 1, 1, 1, 1, 0, 1}, { 0.9f, -0.7f, 0.5f, 1, 1, 1, 1, 1, 1, 1},
};
static const uint16_t kIdx[18] = {0,1,2,1,3,2, 4,5,6,5,7,6, 8,9,10,9,11,10};
static const uint32_t kTexels[4] = {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};   /* red, green, blue, white (ABGR) */

static int map_write4(void* ctx, void* buf, float a, float b, float c, float d, float e, float f, float g, float h) {
  uint8_t mapped[16]; zero(mapped, sizeof mapped);
  if (((F_Map)VT(ctx)[14])(ctx, buf, 0, 4 /* WRITE_DISCARD */, 0, mapped) < 0) return 0;
  float* p = *(float**)mapped; if (!p) return 0;
  p[0] = a; p[1] = b; p[2] = c; p[3] = d; p[4] = e; p[5] = f; p[6] = g; p[7] = h;
  ((F_Unmap)VT(ctx)[15])(ctx, buf, 0);
  return 1;
}

MS uint32_t entry(void) {
  uint8_t wc[80]; zero(wc, sizeof wc);
  put32(wc, 0, 80); put64(wc, 8, (uint64_t)(uintptr_t)&wnd_proc); put64(wc, 64, (uint64_t)(uintptr_t)kClass);
  if (!RegisterClassExA(wc)) return 1;
  uint64_t hwnd = CreateWindowExA(0, kClass, "Anyxbox scene", 0x00CF0000, 100, 100, 320, 240, 0, 0, 0, 0);
  if (!hwnd) return 2;
  ShowWindow(hwnd, 5);

  uint8_t sd[72]; zero(sd, sizeof sd);
  put32(sd, 0, 320); put32(sd, 4, 240); put32(sd, 16, 28); put32(sd, 28, 1); put32(sd, 36, 0x20); put32(sd, 40, 1); put64(sd, 48, hwnd); put32(sd, 56, 1);
  void *swap = 0, *dev = 0, *ctx = 0; uint32_t level = 0;
  if (D3D11CreateDeviceAndSwapChain(0, 1, 0, 0, 0, 0, 7, sd, &swap, &dev, &level, &ctx) < 0) return 3;
  void *bb = 0, *rtv = 0;
  if (((F_GetBuffer)VT(swap)[9])(swap, 0, kIidTexture2D, &bb) < 0) return 4;
  if (((F_CreateView)VT(dev)[9])(dev, bb, 0, &rtv) < 0) return 5;

  /* depth buffer, depth state (LESS), rasterizer (solid, no culling) */
  uint8_t td[44]; zero(td, sizeof td); put32(td, 0, 320); put32(td, 4, 240); put32(td, 8, 1); put32(td, 12, 1); put32(td, 16, 40 /* D32_FLOAT */); put32(td, 20, 1); put32(td, 32, 0x40);
  void *dtex = 0, *dsv = 0, *dss = 0, *rs = 0;
  if (((F_CreateTex)VT(dev)[5])(dev, td, 0, &dtex) < 0) return 6;
  if (((F_CreateView)VT(dev)[10])(dev, dtex, 0, &dsv) < 0) return 7;
  uint8_t dd[64]; zero(dd, sizeof dd); put32(dd, 0, 1); put32(dd, 4, 1); put32(dd, 8, 2);
#ifdef DRAW_COPY_FIXTURE
  put32(dd, 8, 4 /* LESS_EQUAL, redraw the same geometry without clearing depth */);
#endif
  if (((F_CreateState)VT(dev)[21])(dev, dd, &dss) < 0) return 8;
  uint8_t rd[64]; zero(rd, sizeof rd); put32(rd, 0, 3); put32(rd, 4, 1);
  if (((F_CreateState)VT(dev)[22])(dev, rd, &rs) < 0) return 9;

  /* vertex and index buffers with initial data */
  uint8_t bd[24]; uint8_t sub[16]; void *vb = 0, *ib = 0;
  zero(bd, sizeof bd); put32(bd, 0, sizeof kVerts); put32(bd, 8, 1); zero(sub, sizeof sub); put64(sub, 0, (uint64_t)(uintptr_t)kVerts);
  if (((F_CreateBuffer)VT(dev)[3])(dev, bd, sub, &vb) < 0) return 10;
  zero(bd, sizeof bd); put32(bd, 0, sizeof kIdx); put32(bd, 8, 2); put64(sub, 0, (uint64_t)(uintptr_t)kIdx);
  if (((F_CreateBuffer)VT(dev)[3])(dev, bd, sub, &ib) < 0) return 11;
  /* two dynamic constant buffers (32 bytes: two float4) */
  void *vcb = 0, *pcb = 0;
  zero(bd, sizeof bd); put32(bd, 0, 32); put32(bd, 4, 2 /* DYNAMIC */); put32(bd, 8, 4 /* CONSTANT_BUFFER */); put32(bd, 12, 0x10000 /* CPU_ACCESS_WRITE */);
  if (((F_CreateBuffer)VT(dev)[3])(dev, bd, 0, &vcb) < 0) return 12;
  if (((F_CreateBuffer)VT(dev)[3])(dev, bd, 0, &pcb) < 0) return 13;

  /* texture, view, sampler */
  uint8_t tx[44]; zero(tx, sizeof tx); put32(tx, 0, 2); put32(tx, 4, 2); put32(tx, 8, 1); put32(tx, 12, 1); put32(tx, 16, 28); put32(tx, 20, 1); put32(tx, 32, 0x8);
#if defined(DRAW_COPY_METHOD) && DRAW_COPY_METHOD == 3
  put32(tx,28,2 /* DYNAMIC */); put32(tx,36,0x10000 /* CPU_ACCESS_WRITE */);
#endif
  void *tex = 0, *srv = 0, *smp = 0;
  if (((F_CreateTex)VT(dev)[5])(dev, tx, 0, &tex) < 0) return 14;
  if (((F_CreateView)VT(dev)[7])(dev, tex, 0, &srv) < 0) return 15;
  uint8_t smd[64]; zero(smd, sizeof smd); put32(smd, 0, 0 /* POINT */); put32(smd, 4, 3); put32(smd, 8, 3); put32(smd, 12, 3);
  if (((F_CreateState)VT(dev)[23])(dev, smd, &smp) < 0) return 16;
#if defined(DRAW_COPY_METHOD) && DRAW_COPY_METHOD == 3
  uint8_t initial_map[16]; zero(initial_map,sizeof initial_map);
  if (((F_Map)VT(ctx)[14])(ctx,tex,0,4,0,initial_map)<0) return 35;
  uint8_t* initial_data=*(uint8_t**)initial_map; uint32_t initial_pitch=*(uint32_t*)(initial_map+8);
  if(!initial_data || initial_pitch<8) return 36;
  for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<2;++x) *(uint32_t*)(initial_data+y*initial_pitch+x*4)=kTexels[y*2+x];
  ((F_Unmap)VT(ctx)[15])(ctx,tex,0);
#else
  ((F_Update)VT(ctx)[48])(ctx, tex, 0, 0, kTexels, 8, 0);
#endif

  /* shaders and layouts */
  uint8_t el[96]; zero(el, sizeof el);
  put64(el, 0, (uint64_t)(uintptr_t)kPos); put32(el, 12, 2); put32(el, 20, 0);
  put64(el, 32, (uint64_t)(uintptr_t)kCol); put32(el, 32 + 12, 2); put32(el, 32 + 20, 16);
  put64(el, 64, (uint64_t)(uintptr_t)kTex); put32(el, 64 + 12, 16 /* R32G32_FLOAT */); put32(el, 64 + 20, 32);
  void *lay_cb = 0, *lay_tex = 0, *vs_cb = 0, *ps_cb = 0, *vs_tex = 0, *ps_tex = 0;
  if (((F_CreateLayout)VT(dev)[11])(dev, el, 2, scene_vs_cb, sizeof scene_vs_cb, &lay_cb) < 0) return 17;
  if (((F_CreateLayout)VT(dev)[11])(dev, el, 3, scene_vs_tex, sizeof scene_vs_tex, &lay_tex) < 0) return 18;
  if (((F_CreateShader)VT(dev)[12])(dev, scene_vs_cb, sizeof scene_vs_cb, 0, &vs_cb) < 0) return 19;
  if (((F_CreateShader)VT(dev)[15])(dev, scene_ps_cb, sizeof scene_ps_cb, 0, &ps_cb) < 0) return 20;
  if (((F_CreateShader)VT(dev)[12])(dev, scene_vs_tex, sizeof scene_vs_tex, 0, &vs_tex) < 0) return 21;
  if (((F_CreateShader)VT(dev)[15])(dev, scene_ps_tex, sizeof scene_ps_tex, 0, &ps_tex) < 0) return 22;

  const float vp[6] = {0, 0, 320, 240, 0, 1};
  const float clear[4] = {0.05f, 0.05f, 0.15f, 1.0f};
  const uint32_t stride = 40, offset = 0;
  int frames = 0, quit = 0;
  while (!quit && frames < FRAMES) {
    MSGX msg;
    while (PeekMessageA(&msg, 0, 0, 0, 1)) {
      if (msg.message == 0x12) { quit = 1; break; }
      DispatchMessageA(&msg);
    }
    if (quit) break;
    ((F_OMSetRT)VT(ctx)[33])(ctx, 1, &rtv, dsv);
    ((F_RSSetVP)VT(ctx)[44])(ctx, 1, vp);
#ifdef DRAW_COPY_FIXTURE
    if (frames == 0)
#endif
    ((F_ClearRTV)VT(ctx)[50])(ctx, rtv, clear);
#ifdef DRAW_COPY_FIXTURE
    if (frames == 0)
#endif
    ((F_ClearDSV)VT(ctx)[53])(ctx, dsv, 1 /* CLEAR_DEPTH */, 1.0f, 0);
    ((F_SetState)VT(ctx)[43])(ctx, rs);
    ((F_SetDSS)VT(ctx)[36])(ctx, dss, 0);
    ((F_SetVB)VT(ctx)[18])(ctx, 0, 1, &vb, &stride, &offset);
    ((F_SetIB)VT(ctx)[19])(ctx, ib, 57 /* R16_UINT */, 0);
    ((F_SetTopo)VT(ctx)[24])(ctx, 4);

    /* pass 1: constant buffers. near quad: identity transform and tint. */
    ((F_SetLayout)VT(ctx)[17])(ctx, lay_cb);
    ((F_SetShader)VT(ctx)[11])(ctx, vs_cb, 0, 0);
    ((F_SetShader)VT(ctx)[9])(ctx, ps_cb, 0, 0);
    ((F_SetSlots)VT(ctx)[7])(ctx, 0, 1, &vcb);
    ((F_SetSlots)VT(ctx)[16])(ctx, 0, 1, &pcb);
    if (!map_write4(ctx, vcb, 1, 1, 1, 1, 0, 0, 0, 0)) return 23;        /* position = v * scale + offset */
    if (!map_write4(ctx, pcb, 1, 1, 1, 1, 0, 0, 0, 0)) return 24;        /* colour = v * scale + offset */
    ((F_DrawIndexed)VT(ctx)[12])(ctx, 6, 0, 0);
    /* far quad: shifted right by 0.5, tinted orange (green channel +0.5). It is drawn second, so depth must keep the near quad in front. */
    if (!map_write4(ctx, vcb, 1, 1, 1, 1, 0.5f, 0, 0, 0)) return 25;
    if (!map_write4(ctx, pcb, 1, 1, 1, 1, 0, 0.5f, 0, 0)) return 26;
    ((F_DrawIndexed)VT(ctx)[12])(ctx, 6, 6, 0);

    /* pass 2: textured quad */
    ((F_SetLayout)VT(ctx)[17])(ctx, lay_tex);
    ((F_SetShader)VT(ctx)[11])(ctx, vs_tex, 0, 0);
    ((F_SetShader)VT(ctx)[9])(ctx, ps_tex, 0, 0);
    ((F_SetSlots)VT(ctx)[10])(ctx, 0, 1, &smp);
    ((F_SetSlots)VT(ctx)[8])(ctx, 0, 1, &srv);
    ((F_DrawIndexed)VT(ctx)[12])(ctx, 6, 12, 0);
    if (((F_Present)VT(swap)[8])(swap, 0, 0) < 0) return 27;
#ifdef DRAW_COPY_FIXTURE
    if (frames == 0) {
      /* Mutate both a sampled resource and a live color target after the first present.
       * The next draw must upload the new color surface instead of restoring old pixels. */
      void *surface = 0;
#if !defined(DRAW_COPY_METHOD) || DRAW_COPY_METHOD == 1
      void *replacement = 0;
#endif
      static const uint32_t yellow[4] = {0xff00ffffu,0xff00ffffu,0xff00ffffu,0xff00ffffu};
#if !defined(DRAW_COPY_METHOD) || DRAW_COPY_METHOD == 1
      if (((F_CreateTex)VT(dev)[5])(dev, tx, 0, &replacement) < 0) return 29;
      ((F_Update)VT(ctx)[48])(ctx, replacement, 0, 0, yellow, 8, 0);
#endif
#if !defined(DRAW_COPY_METHOD) || DRAW_COPY_METHOD == 3
      typedef void (MS *Copy)(void*, void*, void*);
#endif
#if defined(DRAW_COPY_METHOD) && DRAW_COPY_METHOD == 1
      typedef void (MS *Region)(void*,void*,uint32_t,uint32_t,uint32_t,uint32_t,void*,uint32_t,const void*);
      const uint32_t box[6]={0,0,0,1,2,1};
      ((Region)VT(ctx)[46])(ctx,tex,0,1,0,0,replacement,0,box);
#elif defined(DRAW_COPY_METHOD) && DRAW_COPY_METHOD == 2
      const uint32_t box[6]={1,0,0,2,2,1};
      ((F_Update)VT(ctx)[48])(ctx,tex,0,box,yellow,8,0);
#elif defined(DRAW_COPY_METHOD) && DRAW_COPY_METHOD == 3
      uint8_t mapped[16]; zero(mapped,sizeof mapped);
      if (((F_Map)VT(ctx)[14])(ctx,tex,0,4,0,mapped)<0) return 31;
      uint32_t pitch=*(uint32_t*)(mapped+8); uint8_t* bytes=*(uint8_t**)mapped;
      if (!bytes || pitch<8) return 32;
      for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<2;++x) *(uint32_t*)(bytes+y*pitch+x*4)=yellow[0];
      ((F_Unmap)VT(ctx)[15])(ctx,tex,0);
#else
      ((Copy)VT(ctx)[47])(ctx, tex, replacement);
#endif
      uint8_t surface_desc[44]; zero(surface_desc, sizeof surface_desc);
      put32(surface_desc,0,320); put32(surface_desc,4,240); put32(surface_desc,8,1);
      put32(surface_desc,12,1); put32(surface_desc,16,28); put32(surface_desc,20,1);
#if defined(DRAW_COPY_METHOD) && DRAW_COPY_METHOD == 3
      put32(surface_desc,28,3 /* STAGING */); put32(surface_desc,36,0x20000 /* CPU_ACCESS_READ */ | 0x10000 /* WRITE */);
#endif
      if (((F_CreateTex)VT(dev)[5])(dev, surface_desc, 0, &surface) < 0) return 30;
      static uint32_t cyan[320*240];
      for (uint32_t i=0;i<320*240;++i) cyan[i]=0xffffff00u;
#if !defined(DRAW_COPY_METHOD) || DRAW_COPY_METHOD != 3
      ((F_Update)VT(ctx)[48])(ctx, surface, 0, 0, cyan, 320*4, 0);
#endif
#if defined(DRAW_COPY_METHOD) && DRAW_COPY_METHOD == 1
      const uint32_t strip[6]={0,0,0,320,20,1};
      ((Region)VT(ctx)[46])(ctx,bb,0,0,0,0,surface,0,strip);
#elif defined(DRAW_COPY_METHOD) && DRAW_COPY_METHOD == 2
      const uint32_t strip[6]={0,0,0,320,20,1};
      ((F_Update)VT(ctx)[48])(ctx,bb,0,strip,cyan,320*4,0);
#elif defined(DRAW_COPY_METHOD) && DRAW_COPY_METHOD == 3
      uint8_t mapped_surface[16]; zero(mapped_surface,sizeof mapped_surface);
      if (((F_Map)VT(ctx)[14])(ctx,surface,0,2 /* WRITE */,0,mapped_surface)<0) return 33;
      uint8_t* data=*(uint8_t**)mapped_surface; uint32_t row=*(uint32_t*)(mapped_surface+8);
      if(!data || row<320*4) return 34;
      for(uint32_t y=0;y<240;++y) for(uint32_t x=0;x<320;++x) *(uint32_t*)(data+y*row+x*4)=cyan[0];
      ((F_Unmap)VT(ctx)[15])(ctx,surface,0);
      ((Copy)VT(ctx)[47])(ctx, bb, surface);
#else
      ((Copy)VT(ctx)[47])(ctx, bb, surface);
#endif
    }
#endif
    frames++;
  }
  return frames == 0 && !quit ? 28 : 0;
}
#ifdef ANYXBOX_HOST_TEST
}
#else
void MS mainEntry(void) { ExitProcess(entry()); }
#endif
