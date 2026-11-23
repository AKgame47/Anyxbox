// Anyxbox: vertex buffer slots above 0 and constant buffer layout packing in the D3D11 model (reference renderer).
#include <cstdio>
#include <cstring>
#include "gfx/d3d11_model.h"
using namespace anyxbox::d3d11;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } }while(0)
int main() {
  const int W = 32, H = 32;
  Device dev; DeviceContext ctx(dev);
  ResourceId tex = dev.CreateTexture2D(W, H, Format::R8G8B8A8_UNORM), rtv = dev.CreateRenderTargetView(tex);
  // positions in slot 0 (float2, stride 8) and colours in slot 1 (float3 at offset 4 of a 16 byte stride, with padding)
  const float pos[3][2] = {{-1, -1}, {1, -1}, {-1, 1}};
  float colp[3][4]; for (int i = 0; i < 3; i++) { colp[i][0] = 9; colp[i][1] = 0; colp[i][2] = 1; colp[i][3] = 0; }   // rgb = (0,1,0)
  ctx.IASetInputLayout({{"POSITION", Format::R32G32_FLOAT, 0, 0}, {"COLOR", Format::R32G32B32_FLOAT, 4, 1}});
  CHECK(ctx.IASetVertexBufferSlot(0, dev.CreateBuffer(pos, sizeof pos), 8, 0));
  CHECK(ctx.IASetVertexBufferSlot(1, dev.CreateBuffer(colp, sizeof colp), 16, 0));
  CHECK(!ctx.IASetVertexBufferSlot(16, 0, 0, 0));
  ctx.IASetPrimitiveTopology(Topology::TriangleList); ctx.RSSetViewport({0, 0, (float)W, (float)H}); ctx.OMSetRenderTarget(rtv);
  const float black[4] = {0, 0, 0, 1}; ctx.ClearRenderTargetView(rtv, black);
  CHECK(ctx.Draw(3, 0));
  std::vector<uint8_t> im; CHECK(dev.ReadTexture(tex, &im));
  const uint8_t* p = &im[((size_t)24 * W + 6) * 4];   // inside the triangle
  CHECK(p[0] == 0 && p[1] == 255 && p[2] == 0);   // colour (0, 1, 0) taken from slot 1 at offset 4
  // a slot used by the layout but not bound is an error
  ctx.IASetInputLayout({{"POSITION", Format::R32G32_FLOAT, 0, 0}, {"COLOR", Format::R32G32B32_FLOAT, 0, 3}});
  CHECK(!ctx.Draw(3, 0));
  // slot 1 buffer too short for the vertices
  ctx.IASetInputLayout({{"POSITION", Format::R32G32_FLOAT, 0, 0}, {"COLOR", Format::R32G32B32_FLOAT, 4, 1}});
  CHECK(ctx.IASetVertexBufferSlot(1, dev.CreateBuffer(colp, 20), 16, 0));
  CHECK(!ctx.Draw(3, 0));
  auto triangles=ctx.stats().triangles_drawn;ctx.ClearState();ctx.ClearState();
  CHECK(ctx.stats().triangles_drawn==triangles);CHECK(!ctx.Draw(3,0));
  CHECK(ctx.last_error()=="draw: only TriangleList is supported");
  ctx.IASetPrimitiveTopology(Topology::TriangleList);CHECK(!ctx.Draw(3,0));
  CHECK(ctx.last_error()=="draw: no valid render target bound");
  ctx.OMSetRenderTarget(rtv);CHECK(!ctx.Draw(3,0));
  CHECK(ctx.last_error()=="draw: viewport not set");
  std::printf("d3d11 slots: all ok\n");
  return 0;
}
