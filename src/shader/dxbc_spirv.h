// Anyxbox: translate a decoded DXBC vertex or pixel shader (straight-line SM4 float subset) to SPIR-V. GPL-2.0.
//
// Supported: vs_4_x / ps_4_x (and 5_x without new instructions); dcl_input, dcl_input_ps, dcl_output,
// dcl_output_siv (position), dcl_input_ps SV_POSITION, dcl_temps, dcl_globalFlags;
// mov add mul mad div dp2 dp3 dp4 min max sqrt rsq frc exp log; masks, swizzles, neg/abs modifiers,
// saturate, 32-bit float immediates; one RET at the end.
// Everything else (control flow, integer ops, resources, samplers, constant buffers, geometry/compute
// stages) is reported as an error naming the opcode, never skipped.
//
// Interface rules for the Vulkan draw backend:
//  - Vertex shader inputs use location = DXBC register number (v0 -> location 0).
//  - Vertex outputs and pixel inputs other than SV_POSITION use locations 0,1,... in ascending register order.
//  - Pixel shader outputs use location = output register number.
//  - Input variables are as wide as the highest component in their signature mask; missing components read
//    as 0,0,0,1 like D3D.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "shader/dxbc.h"
namespace anyxbox { namespace shader {
struct SpirvResult { bool ok = false; std::string error; std::vector<uint32_t> words; bool is_pixel = false; };
SpirvResult dxbc_to_spirv(const uint8_t* dxbc, size_t size);
}}
