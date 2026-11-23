// Independently compiled against public Windows headers. No scene or private runtime declarations.
#include <d3d11.h>
#include <cstdio>
#include <cstring>
#define CHECK(c) do{if(!(c)){std::printf("resource interfaces failed line %d\n",__LINE__);return 3;}}while(0)
int main(){
  ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx)));
  ID3D11Device* sameDev=nullptr;CHECK(SUCCEEDED(dev->QueryInterface(__uuidof(ID3D11Device),(void**)&sameDev))&&sameDev==dev);sameDev->Release();
  ID3D11DeviceContext* sameCtx=nullptr;CHECK(SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext),(void**)&sameCtx))&&sameCtx==ctx);sameCtx->Release();
  ctx->GetDevice(&sameDev);CHECK(sameDev==dev);sameDev->Release();
  D3D11_BUFFER_DESC bd={};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
  ID3D11Buffer* buffer=nullptr;CHECK(SUCCEEDED(dev->CreateBuffer(&bd,nullptr,&buffer)));
  D3D11_TEXTURE2D_DESC td={};td.Width=4;td.Height=4;td.MipLevels=1;td.ArraySize=1;td.SampleDesc.Count=1;
  td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  ID3D11Texture2D* texture=nullptr;CHECK(SUCCEEDED(dev->CreateTexture2D(&td,nullptr,&texture)));
  td.Format=DXGI_FORMAT_D32_FLOAT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
  ID3D11Texture2D* depth=nullptr;CHECK(SUCCEEDED(dev->CreateTexture2D(&td,nullptr,&depth)));
  D3D11_BUFFER_DESC gotBuffer={};buffer->GetDesc(&gotBuffer);CHECK(std::memcmp(&gotBuffer,&bd,sizeof(bd))==0);
  D3D11_TEXTURE2D_DESC gotTexture={};depth->GetDesc(&gotTexture);CHECK(std::memcmp(&gotTexture,&td,sizeof(td))==0);
  td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  texture->GetDesc(&gotTexture);CHECK(std::memcmp(&gotTexture,&td,sizeof(td))==0);
  td.MipLevels=0;td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ID3D11Texture2D* staging=nullptr;CHECK(SUCCEEDED(dev->CreateTexture2D(&td,nullptr,&staging)));
  staging->GetDesc(&gotTexture);td.MipLevels=3;CHECK(std::memcmp(&gotTexture,&td,sizeof(td))==0);staging->Release();
  std::puts("resource GetDesc: buffer/color/depth/staging and resolved full-mip fields ok");
  ID3D11Resource* resources[]={buffer,texture,depth};
  for(unsigned i=0;i<3;++i){
    ID3D11Resource* resource=nullptr;CHECK(SUCCEEDED(resources[i]->QueryInterface(__uuidof(ID3D11Resource),(void**)&resource)));
    D3D11_RESOURCE_DIMENSION type=D3D11_RESOURCE_DIMENSION_UNKNOWN;resource->GetType(&type);
    CHECK(type==(i==0?D3D11_RESOURCE_DIMENSION_BUFFER:D3D11_RESOURCE_DIMENSION_TEXTURE2D));
    ID3D11DeviceChild* child=nullptr;CHECK(SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11DeviceChild),(void**)&child)));
    child->GetDevice(&sameDev);CHECK(sameDev==dev);sameDev->Release();
    IUnknown* a=nullptr;IUnknown* b=nullptr;
    CHECK(SUCCEEDED(resource->QueryInterface(__uuidof(IUnknown),(void**)&a)));
    CHECK(SUCCEEDED(child->QueryInterface(__uuidof(IUnknown),(void**)&b))&&a==b);
    void* typed=nullptr;CHECK(SUCCEEDED(child->QueryInterface(i==0?__uuidof(ID3D11Buffer):__uuidof(ID3D11Texture2D),&typed))&&typed==resources[i]);
    ((IUnknown*)typed)->Release();
    typed=(void*)1;CHECK(child->QueryInterface(__uuidof(ID3D11Device),&typed)==E_NOINTERFACE&&typed==nullptr);
    a->Release();b->Release();child->Release();resource->Release();
  }
  depth->Release();texture->Release();buffer->Release();ctx->Release();dev->Release();
  std::puts("resource interfaces: device/context, buffer/color/depth identity, type and GetDevice ok");return 0;
}
