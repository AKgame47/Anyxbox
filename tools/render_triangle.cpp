// render_triangle <out.ppm>: draws the colour triangle through the D3D11-style model and writes a PPM.
#include <cstdio>
#include "gfx/d3d11_model.h"
using namespace anyxbox::d3d11;
int main(int argc, char** argv) {
  if (argc != 2) { std::puts("usage: render_triangle <out.ppm>"); return 2; }
  const int W = 256, H = 256;
  float v[] = {-0.8f,-0.8f,0, 1,0,0,1,   0.8f,-0.8f,0, 0,1,0,1,   0.0f,0.8f,0, 0,0,1,1};
  Device d; DeviceContext c(d);
  auto tex = d.CreateTexture2D(W, H, Format::R8G8B8A8_UNORM); auto rtv = d.CreateRenderTargetView(tex);
  const float clear[4] = {0.05f, 0.05f, 0.08f, 1};
  c.ClearRenderTargetView(rtv, clear);
  c.IASetInputLayout({{"POSITION", Format::R32G32B32_FLOAT, 0}, {"COLOR", Format::R32G32B32A32_FLOAT, 12}});
  c.IASetVertexBuffer(d.CreateBuffer(v, sizeof v), 28, 0);
  c.IASetPrimitiveTopology(Topology::TriangleList);
  c.RSSetViewport({0, 0, (float)W, (float)H}); c.OMSetRenderTarget(rtv);
  if (!c.Draw(3, 0)) { std::puts(c.last_error().c_str()); return 1; }
  std::vector<uint8_t> im; d.ReadTexture(tex, &im);
  FILE* f = std::fopen(argv[1], "wb"); if (!f) return 1;
  std::fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) std::fwrite(&im[i * 4], 1, 3, f);
  std::fclose(f);
  std::printf("pixels written: %llu\n", (unsigned long long)c.stats().pixels_written);
  return 0;
}
