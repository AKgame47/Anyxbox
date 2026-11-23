// Anyxbox: Direct3D 11 COM layer for guest programs. GPL-2.0.
// A guest PE imports D3D11CreateDevice from d3d11.dll; this module hands back host objects whose vtables
// hold ms_abi functions, so the program's own machine code calls through them. Calls land in the
// D3D11 model (src/gfx) and its draw backend. Shaders are translated from DXBC to SPIR-V on creation.
// Slice supported: Device (CreateBuffer, CreateTexture2D, CreateRenderTargetView, CreateInputLayout,
// CreateVertexShader, CreatePixelShader, GetImmediateContext, GetFeatureLevel) and the context calls to
// bind and Draw a triangle list with cbuffer slot 0. Everything else in the vtables returns E_NOTIMPL.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "gfx/d3d11_model.h"
#include "syslayer/import_registry.h"
namespace anyxbox {
struct D3D11Runtime {
  d3d11::Device dev;
  std::unique_ptr<d3d11::DeviceContext> ctx;
  d3d11::DrawBackend* backend = nullptr;   // null = built-in reference renderer (ignores shader bytecode)
  // Called right before a Draw when both shaders are bound (translated SPIR-V words).
  std::function<void(const std::vector<uint32_t>& vs, const std::vector<uint32_t>& ps)> on_shaders;
  std::string last_error;                  // last translation or call failure, for tests and logs
  int shaders_translated = 0;
  D3D11Runtime();
  ~D3D11Runtime();
  // Registers D3D11CreateDevice under d3d11.dll. The runtime must outlive the guest run.
  void register_imports(ImportRegistry& reg);
  struct Impl;
  std::unique_ptr<Impl> impl;
};
}
