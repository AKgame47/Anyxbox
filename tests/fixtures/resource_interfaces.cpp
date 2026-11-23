// Independently compiled against public Windows headers. No scene or private runtime declarations.
#include <d3d11.h>
#include <cstdio>
#include <cstring>
#include "triangle_shaders.h"
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
  ID3D11Texture2D* owned=nullptr;td.MipLevels=1;td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;
  td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  CHECK(SUCCEEDED(dev->CreateTexture2D(&td,nullptr,&owned)));
  ID3D11RenderTargetView* rtv=nullptr;ID3D11ShaderResourceView* srv=nullptr;ID3D11DepthStencilView* dsv=nullptr;
  CHECK(SUCCEEDED(dev->CreateRenderTargetView(owned,nullptr,&rtv)));
  CHECK(SUCCEEDED(dev->CreateShaderResourceView(owned,nullptr,&srv)));
  CHECK(SUCCEEDED(dev->CreateDepthStencilView(depth,nullptr,&dsv)));
  ID3D11View* views[]={rtv,srv,dsv};ID3D11Resource* expected[]={owned,owned,depth};
  owned->Release();
  for(unsigned i=0;i<3;++i){
    ID3D11View* baseView=nullptr;CHECK(SUCCEEDED(views[i]->QueryInterface(__uuidof(ID3D11View),(void**)&baseView)));
    void* concrete=nullptr;const GUID* id=i==0?&__uuidof(ID3D11RenderTargetView):i==1?&__uuidof(ID3D11ShaderResourceView):&__uuidof(ID3D11DepthStencilView);
    CHECK(SUCCEEDED(baseView->QueryInterface(*id,&concrete))&&concrete==views[i]);((IUnknown*)concrete)->Release();
    ID3D11Resource* returned=nullptr;baseView->GetResource(&returned);CHECK(returned==expected[i]);
    baseView->Release();views[i]->Release();
    D3D11_RESOURCE_DIMENSION type=D3D11_RESOURCE_DIMENSION_UNKNOWN;returned->GetType(&type);CHECK(type==D3D11_RESOURCE_DIMENSION_TEXTURE2D);
    returned->Release();
  }
  std::puts("view GetResource: RTV/SRV/DSV identity and retained resource lifetime ok");
  ID3D11Texture2D* target=nullptr;ID3D11RenderTargetView* boundRT=nullptr;ID3D11DepthStencilView* boundDS=nullptr;
  CHECK(SUCCEEDED(dev->CreateTexture2D(&td,nullptr,&target)));
  CHECK(SUCCEEDED(dev->CreateRenderTargetView(target,nullptr,&boundRT)));
  CHECK(SUCCEEDED(dev->CreateDepthStencilView(depth,nullptr,&boundDS)));target->Release();
  ctx->OMSetRenderTargets(1,&boundRT,boundDS);
  CHECK(boundRT->Release()==1&&boundDS->Release()==1);
  ctx->OMSetRenderTargets(1,&boundRT,boundDS);
  ID3D11Resource* held=nullptr;boundRT->GetResource(&held);D3D11_RESOURCE_DIMENSION heldType=D3D11_RESOURCE_DIMENSION_UNKNOWN;held->GetType(&heldType);CHECK(heldType==D3D11_RESOURCE_DIMENSION_TEXTURE2D);
  held->Release();
  CHECK(boundRT->AddRef()==2&&boundDS->AddRef()==2);
  ctx->OMSetRenderTargets(0,nullptr,nullptr);
  CHECK(boundRT->Release()==0&&boundDS->Release()==0);
  std::puts("output merger: RTV/DSV caller release, same-object rebind and unbind references ok");
  ID3D11ShaderResourceView* inputs[2]={};
  CHECK(SUCCEEDED(dev->CreateShaderResourceView(texture,nullptr,&inputs[0])));
  CHECK(SUCCEEDED(dev->CreateShaderResourceView(texture,nullptr,&inputs[1])));
  ctx->PSSetShaderResources(0,2,inputs);
  CHECK(inputs[0]->Release()==1&&inputs[1]->Release()==1);
  ID3D11ShaderResourceView* swapInputs[]={inputs[1],inputs[0]};
  ctx->PSSetShaderResources(0,2,swapInputs);ctx->PSSetShaderResources(0,2,swapInputs);
  CHECK(inputs[0]->AddRef()==2&&inputs[1]->AddRef()==2);
  ctx->PSSetShaderResources(0,2,nullptr);
  CHECK(inputs[0]->Release()==0&&inputs[1]->Release()==0);
  std::puts("pixel SRV bindings: caller release, cross-slot swap, rebind and unbind references ok");
  ID3D11Buffer* vertices[2]={};bd.BindFlags=D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_INDEX_BUFFER;
  CHECK(SUCCEEDED(dev->CreateBuffer(&bd,nullptr,&vertices[0])));
  CHECK(SUCCEEDED(dev->CreateBuffer(&bd,nullptr,&vertices[1])));
  UINT strides[]={16,16},offsets[]={0,0};ctx->IASetVertexBuffers(0,2,vertices,strides,offsets);
  CHECK(vertices[0]->Release()==1&&vertices[1]->Release()==1);
  ID3D11Buffer* swappedVertices[]={vertices[1],vertices[0]};
  ctx->IASetVertexBuffers(0,2,swappedVertices,strides,offsets);
  ctx->IASetVertexBuffers(0,2,swappedVertices,strides,offsets);
  ctx->IASetIndexBuffer(vertices[0],DXGI_FORMAT_R16_UINT,0);
  CHECK(vertices[0]->AddRef()==3&&vertices[1]->AddRef()==2);
  ID3D11Buffer* nullVertices[]={nullptr,nullptr};ctx->IASetVertexBuffers(0,2,nullVertices,strides,offsets);
  CHECK(vertices[0]->Release()==1&&vertices[1]->Release()==0);
  ctx->IASetIndexBuffer(vertices[0],DXGI_FORMAT_R16_UINT,0);
  CHECK(vertices[0]->AddRef()==2);ctx->IASetIndexBuffer(nullptr,DXGI_FORMAT_R16_UINT,0);
  CHECK(vertices[0]->Release()==0);
  std::puts("input buffers: vertex caller release, swap, rebind and index retain/unbind references ok");
  for(unsigned stage=0;stage<2;++stage){
    ID3D11Buffer* constants[2]={};bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    CHECK(SUCCEEDED(dev->CreateBuffer(&bd,nullptr,&constants[0])));
    CHECK(SUCCEEDED(dev->CreateBuffer(&bd,nullptr,&constants[1])));
    if(stage)ctx->PSSetConstantBuffers(0,2,constants);else ctx->VSSetConstantBuffers(0,2,constants);
    CHECK(constants[0]->Release()==1&&constants[1]->Release()==1);
    ID3D11Buffer* swappedConstants[]={constants[1],constants[0]};
    for(unsigned repeat=0;repeat<2;++repeat){
      if(stage)ctx->PSSetConstantBuffers(0,2,swappedConstants);else ctx->VSSetConstantBuffers(0,2,swappedConstants);
    }
    CHECK(constants[0]->AddRef()==2&&constants[1]->AddRef()==2);
    if(stage)ctx->PSSetConstantBuffers(0,2,nullptr);else ctx->VSSetConstantBuffers(0,2,nullptr);
    CHECK(constants[0]->Release()==0&&constants[1]->Release()==0);
  }
  std::puts("constant buffers: VS/PS caller release, cross-slot swap, rebind and unbind references ok");
  ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;
  CHECK(SUCCEEDED(dev->CreateVertexShader(triangle_vs,sizeof(triangle_vs),nullptr,&vs)));
  CHECK(SUCCEEDED(dev->CreatePixelShader(triangle_ps,sizeof(triangle_ps),nullptr,&ps)));
  ctx->VSSetShader(vs,nullptr,0);ctx->PSSetShader(ps,nullptr,0);
  CHECK(vs->Release()==1&&ps->Release()==1);
  ctx->VSSetShader(vs,nullptr,0);ctx->PSSetShader(ps,nullptr,0); // same bound object still alive
  CHECK(vs->AddRef()==2&&ps->AddRef()==2);
  ctx->VSSetShader(nullptr,nullptr,0);ctx->PSSetShader(nullptr,nullptr,0);
  CHECK(vs->Release()==0&&ps->Release()==0);
  std::puts("shader bindings: caller release, same-object rebind and unbind references ok");
  depth->Release();texture->Release();buffer->Release();ctx->Release();dev->Release();
  std::puts("resource interfaces: device/context, buffer/color/depth identity, type and GetDevice ok");return 0;
}
