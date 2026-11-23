// Anyxbox PE/COFF header check. GPL-2.0.
#pragma once
#include <cstddef>
#include <cstdint>
namespace anyxbox {
struct PeInfo { bool ok; bool is64; uint16_t sections; uint64_t image_base; uint32_t entry_rva; };
PeInfo parse_pe_header(const uint8_t* data, size_t size);
}
