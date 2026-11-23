# D3D11 vtable slots used by src/runtime/d3d11_runtime.cpp

Slots are positions in ID3D11DeviceVtbl and ID3D11DeviceContextVtbl (IUnknown = 0..2). Sources checked:
- mingw-w64 `d3d11.h` member order, as cited by https://reverseengineering.wiki/index.php/Fact:Direct3D_11/enum/ID3D11Device_vtable_slots (device slots 3..42).
- https://xosh.org/id3d10device-vtable/ (context slots).

Device: CreateBuffer 3, CreateTexture2D 5, CreateRenderTargetView 9, CreateInputLayout 11, CreateVertexShader 12,
CreatePixelShader 15, GetFeatureLevel 37, GetImmediateContext 40.
Context: VSSetConstantBuffers 7, PSSetShader 9, VSSetShader 11, Draw 13, PSSetConstantBuffers 16, IASetInputLayout 17,
IASetVertexBuffers 18, IASetPrimitiveTopology 24, OMSetRenderTargets 33, RSSetViewports 44, ClearRenderTargetView 50.
Every other slot returns E_NOTIMPL. Struct layouts (BUFFER_DESC, TEXTURE2D_DESC, INPUT_ELEMENT_DESC, VIEWPORT) follow the public headers.
