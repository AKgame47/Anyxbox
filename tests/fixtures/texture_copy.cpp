// Anyxbox fixture: separately compiled D3D11 texture copies, mapping and mip generation.
#include <d3d11.h>
#include <cstdio>
#include <cstring>
#define CHECK(c) do { if (!(c)) { std::printf("texture copy failed line %d\n",__LINE__); return 3; } } while(0)
int main() {
  ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx)));
  D3D11_TEXTURE2D_DESC d = {};
  d.Width=4; d.Height=4; d.MipLevels=3; d.ArraySize=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
  d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
  d.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
  ID3D11Texture2D* src=nullptr; ID3D11Texture2D* dst=nullptr; ID3D11Texture2D* staging=nullptr;
  CHECK(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&src)));
  CHECK(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&dst)));
  d.Usage=D3D11_USAGE_STAGING; d.BindFlags=0; d.MiscFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  CHECK(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&staging)));
  unsigned char pixels[64]; for(unsigned i=0;i<sizeof pixels;++i) pixels[i]=(unsigned char)i;
  ctx->UpdateSubresource(src,0,nullptr,pixels,16,0);
  ctx->UpdateSubresource(src,1,nullptr,pixels,8,0);
  ctx->UpdateSubresource(src,2,nullptr,pixels,4,0);
  ctx->CopyResource(dst,src); ctx->CopyResource(staging,dst);
  for(unsigned l=0;l<3;++l) {
    D3D11_MAPPED_SUBRESOURCE m={}; CHECK(SUCCEEDED(ctx->Map(staging,l,D3D11_MAP_READ,0,&m)));
    unsigned w=4>>l;
    for(unsigned y=0;y<w;++y) CHECK(std::memcmp((unsigned char*)m.pData+y*m.RowPitch,pixels+y*w*4,w*4)==0);
    ctx->Unmap(staging,l);
  }
  std::puts("copy resource mips ok");
  D3D11_BOX box={1,1,0,3,3,1};
  ctx->CopySubresourceRegion(dst,0,0,0,0,src,0,&box); ctx->CopyResource(staging,dst);
  D3D11_MAPPED_SUBRESOURCE m={}; CHECK(SUCCEEDED(ctx->Map(staging,0,D3D11_MAP_READ,0,&m)));
  CHECK(std::memcmp(m.pData,pixels+20,8)==0);
  CHECK(std::memcmp((unsigned char*)m.pData+m.RowPitch,pixels+36,8)==0);
  ctx->Unmap(staging,0); std::puts("copy region ok");
  std::memset(pixels,120,sizeof pixels); ctx->UpdateSubresource(src,0,nullptr,pixels,16,0);
  ID3D11ShaderResourceView* view=nullptr; CHECK(SUCCEEDED(dev->CreateShaderResourceView(src,nullptr,&view)));
  ctx->GenerateMips(view); ctx->CopyResource(staging,src);
  CHECK(SUCCEEDED(ctx->Map(staging,2,D3D11_MAP_READ,0,&m)));
  CHECK(((unsigned char*)m.pData)[0]==120 && ((unsigned char*)m.pData)[3]==120);
  ctx->Unmap(staging,2); std::puts("generated mip copy ok");
  view->Release(); staging->Release(); dst->Release(); src->Release(); ctx->Release(); dev->Release();
  std::puts("ok"); return 0;
}
