// Anyxbox PE32+ image mapper: sections, base relocations, imports. GPL-2.0.
// Maps into a host buffer and patches it. Does not execute anything.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace anyxbox {

struct ImportRef { std::string dll; std::string name; uint16_t ordinal; bool by_ordinal; uint32_t iat_rva; };

// Returns the host address for an import, or 0 if unknown.
using ImportResolver = std::function<uint64_t(const std::string& dll, const std::string& name, uint16_t ordinal, bool by_ordinal)>;

struct SectionInfo { uint32_t rva; uint32_t size; uint32_t flags; };  // flags = IMAGE_SCN_* characteristics

struct LoadedImage {
  bool ok = false;
  std::string error;
  std::vector<uint8_t> mem;      // mapped image when no destination buffer is given
  uint8_t* base = nullptr;       // start of mapped image (mem.data() or the caller's buffer)
  uint32_t image_size = 0;
  uint64_t preferred_base = 0;
  uint64_t actual_base = 0;      // address of mem.data()
  uint32_t entry_rva = 0;
  uint32_t reloc_count = 0;
  std::vector<SectionInfo> sections;
  std::vector<ImportRef> imports;
  std::vector<std::string> unresolved;  // "dll!name" entries the resolver rejected
};

// If dest is given it must hold at least SizeOfImage bytes; the image is mapped there and `mem` stays empty.
LoadedImage load_pe_image(const uint8_t* data, size_t size, const ImportResolver& resolver, uint8_t* dest = nullptr, size_t dest_len = 0);
// SizeOfImage from the headers, or 0 if the headers are invalid.
uint32_t pe_image_size(const uint8_t* data, size_t size);
}
