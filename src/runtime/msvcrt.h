// Anyxbox msvcrt-lite: the small C runtime surface programs from a real compiler usually need. GPL-2.0.
// Registered under msvcrt.dll, ucrtbase.dll and vcruntime140.dll. Windows x64 calling convention.
#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include "syslayer/import_registry.h"
namespace anyxbox {
struct MsvcrtContext {
  std::string* out = nullptr;           // captured stdout
  std::mutex m;
  std::map<uintptr_t, size_t> heap;     // live allocations
  size_t heap_limit = size_t(1) << 30;  // per-process cap
  size_t heap_used = 0;
};
// Registers: memcpy, memmove, memset, memcmp, strlen, strcmp, strncmp, strcpy-free (not provided),
// malloc, calloc, realloc, free, puts, putchar, printf (%d %i %u %x %X %c %s %p %% with l/ll/z/h
// modifiers, width and zero/left flags), abs.
// Context must outlive all calls. Returns false on hosts that cannot run Windows-ABI code.
bool msvcrt_register(ImportRegistry& reg, MsvcrtContext& ctx);
}
