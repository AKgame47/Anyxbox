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
  // Called by exit/_amsg_exit/abort (does not return when set by run_pe). Null: the exit code is only recorded.
  void (*exit_fn)(uint32_t) = nullptr;
  bool exited = false; uint32_t exit_code = 0;
};
// Registers: memcpy, memmove, memset, memcmp, strlen, strcmp, strncmp, strcpy-free (not provided),
// malloc, calloc, realloc, free, puts, putchar, printf (%d %i %u %x %X %c %s %p %% with l/ll/z/h
// modifiers, width and zero/left flags), abs.
// C runtime startup set (what a default MinGW program imports): __getmainargs, __set_app_type, __setusermatherr, _initterm,
// _onexit, _cexit, exit, abort, _amsg_exit, signal, _lock/_unlock, _errno, strerror, localeconv, __lconv_init, ___lc_codepage_func,
// ___mb_cur_max_func, wcslen, __C_specific_handler (stub), the data imports _fmode _commode _acmdln __initenv, and stdio on the
// three standard streams: __iob_func, fwrite, fputc, fprintf, vfprintf (stdout and stderr go to the captured output).
// printf-lite now also handles precision, '*', '+', ' ', '#' and floating point (%f %e %g).
// Context must outlive all calls. Returns false on hosts that cannot run Windows-ABI code.
bool msvcrt_register(ImportRegistry& reg, MsvcrtContext& ctx);
}
