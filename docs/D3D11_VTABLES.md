# D3D11 vtable slots used by src/runtime/d3d11_runtime.cpp

Slots are positions in ID3D11DeviceVtbl and ID3D11DeviceContextVtbl (IUnknown = 0..2). Sources checked:
- mingw-w64 `d3d11.h` member order, as cited by https://reverseengineering.wiki/index.php/Fact:Direct3D_11/enum/ID3D11Device_vtable_slots (device slots 3..42).
- https://xosh.org/id3d10device-vtable/ (context slots).

Device: CreateBuffer 3, CreateTexture2D 5, CreateRenderTargetView 9, CreateInputLayout 11, CreateVertexShader 12,
CreatePixelShader 15, GetFeatureLevel 37, GetImmediateContext 40.
Context: VSSetConstantBuffers 7, PSSetShader 9, VSSetShader 11, Draw 13, PSSetConstantBuffers 16, IASetInputLayout 17,
IASetVertexBuffers 18, IASetPrimitiveTopology 24, OMSetRenderTargets 33, RSSetViewports 44, ClearRenderTargetView 50.
Every other slot returns E_NOTIMPL. Struct layouts (BUFFER_DESC, TEXTURE2D_DESC, INPUT_ELEMENT_DESC, VIEWPORT) follow the public headers.

## Added with depth/blend/cull and the swap chain
Device: CreateDepthStencilView 10, CreateBlendState 20, CreateDepthStencilState 21, CreateRasterizerState 22.
Context: OMSetBlendState 35, OMSetDepthStencilState 36, RSSetState 43, ClearDepthStencilView 53.
IDXGISwapChain (https://reverseengineering.wiki/index.php/Fact:DXGI/enum/IDXGISwapChain_vtable_slots): Present 8, GetBuffer 9.
Not supported (state creation returns E_NOTIMPL and sets last_error): stencil, alpha-to-coverage, partial write masks, BLEND_FACTOR /
SRC_ALPHA_SAT / dual-source blend factors, wireframe fill, depth bias, scissor. The only depth format is D32_FLOAT (or R32_TYPELESS with the depth bind flag).

## Textures, samplers, index buffers
Device: CreateShaderResourceView 7, CreateSamplerState 23. Context: PSSetShaderResources 8, PSSetSamplers 10, DrawIndexed 12, Map 14, Unmap 15,
IASetIndexBuffer 19, UpdateSubresource 48 (same listings as above; mingw-w64 d3d11.h order).
Supported: SRV of a 2D texture at t0, a point or linear sampler at s0 with WRAP or CLAMP (U and V equal), 16 and 32 bit indices, buffers and RGBA8/BGRA8
textures for UpdateSubresource and Map (all five map types use a staging copy that Unmap writes back). MipLevels 0 or 1 creates the base level only.
