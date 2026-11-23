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

## Constant buffer slots, vertex buffer slots, dxgi.dll factory
Context: VSSetConstantBuffers 7 / PSSetConstantBuffers 16 now accept slots 0..13; IASetVertexBuffers 18 accepts slots 0..15 (per-vertex data only, no instancing);
input elements carry their slot. The context packs the bound constant buffers for the translator (see SetConstantLayout in gfx/d3d11_model.h).
dxgi.dll exports CreateDXGIFactory and CreateDXGIFactory1. The factory object implements: MakeWindowAssociation 8 (ok, no-op), CreateSwapChain 10 (same headless swap chain as
D3D11CreateDeviceAndSwapChain), EnumAdapters 7 (returns DXGI_ERROR_NOT_FOUND, no adapters are exposed), IsCurrent 13. All other slots return E_NOTIMPL.
Slots: https://reverseengineering.wiki/index.php/Fact:DXGI/enum/IDXGIFactory_vtable_slots (mingw-w64 dxgi.h / dxgi1_2.h member order).
Not supported yet: more than one texture/sampler (t0/s0 only), instanced vertex data, GetParent/QueryInterface for other DXGI interface ids on the factory.

## Several textures
PSSetShaderResources / PSSetSamplers accept slots 0..3. The translator maps tN to a combined image sampler at set 0 binding N and reports which sampler slot each
texture is sampled with (SpirvResult::tex_sampler, one sampler per texture); the context pairs them (SetTexturePairing, set by the COM layer before every draw).
A slot the shader samples (or a lower slot than the highest sampled one) that has no texture bound reads as zeros, like D3D. Vulkan backend: t0 keeps the full
single-texture path; t1..t3 are plain 2D RGBA8, one level, up to 256x256, with nearest/linear and clamp/wrap from their sampler. Not supported: arrays, cube/3D,
mips for t1..t3, comparison samplers, texture sampling in the vertex shader, SRV format reinterpretation.
