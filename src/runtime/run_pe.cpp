#include "runtime/run_pe.h"
#include <csetjmp>
#include <cstring>
#include "loader/pe_image.h"
#include "syslayer/import_registry.h"
#include "runtime/kernel32.h"
#include "runtime/msvcrt.h"
#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#define ANYXBOX_CAN_RUN 1
#define MSABI __attribute__((ms_abi))
#else
#define ANYXBOX_CAN_RUN 0
#define MSABI
#endif
namespace anyxbox {
#if ANYXBOX_CAN_RUN
namespace {
thread_local std::jmp_buf* g_exit = nullptr;
thread_local uint32_t g_exit_code = 0;
MSABI void host_ExitProcess(uint32_t code) {
  g_exit_code = code;
  if (g_exit) std::longjmp(*g_exit, 1);
  __builtin_trap();
}
typedef uint32_t (MSABI *EntryFn)();
}  // namespace
#endif

RunResult run_pe(const uint8_t* data, size_t size, bool trust) {
  RunResult res;
#if !ANYXBOX_CAN_RUN
  (void)data; (void)size; (void)trust;
  res.error = "running programs is only supported on Linux x86-64 hosts for now";
  return res;
#else
  if (!trust) { res.error = "refusing to run: binary not marked as trusted"; return res; }
  uint32_t isz = pe_image_size(data, size);
  if (!isz) { res.error = "invalid PE headers"; return res; }
  size_t len = ((size_t)isz + 4095) & ~size_t(4095);
  void* mem = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) { res.error = "mmap failed"; return res; }
  ImportRegistry reg;
  Kernel32Context k32;
  std::string out;
  k32.out = &out;
  kernel32_register(reg, k32);
  MsvcrtContext crt;
  crt.out = &out;
  msvcrt_register(reg, crt);
  reg.add("kernel32.dll", "ExitProcess", (uint64_t)(uintptr_t)&host_ExitProcess);
  auto img = load_pe_image(data, size, [&](const std::string& d, const std::string& n, uint16_t o, bool b) {
    return reg.resolve(d, n, o, b); }, (uint8_t*)mem, len);
  res.unresolved = img.unresolved;
  if (!img.ok) { res.error = img.error; munmap(mem, len); return res; }
  res.loaded = true;
  // Protections: headers read-only, each section by its own flags. Data sections stay non-executable
  // and code sections stay read-only. All patching (relocations, imports) is already done.
  {
    const size_t P = 4096;
    if (mprotect(mem, len, PROT_READ) != 0) { res.error = "mprotect failed"; munmap(mem, len); return res; }
    uint32_t last_end = 0;
    for (auto& sc : img.sections) {
      if (sc.rva % P != 0 || sc.rva < last_end) { res.error = "unsupported section layout (not page aligned or overlapping)"; munmap(mem, len); return res; }
      size_t slen = ((size_t)sc.size + P - 1) & ~(P - 1);
      last_end = sc.rva + (uint32_t)slen;
      int prot = PROT_READ;
      if (sc.flags & 0x80000000u) prot |= PROT_WRITE;
      if (sc.flags & 0x20000000u) prot |= PROT_EXEC;
      if (slen && mprotect((uint8_t*)mem + sc.rva, slen, prot) != 0) { res.error = "mprotect failed"; munmap(mem, len); return res; }
    }
  }
  std::jmp_buf jb;
  k32.image_base = img.actual_base;
  g_exit = &jb; g_exit_code = 0;
  EntryFn fn = (EntryFn)(uintptr_t)(img.actual_base + img.entry_rva);
  volatile bool exited = false;
  if (setjmp(jb) == 0) { res.exit_code = fn(); } else { exited = true; res.exit_code = g_exit_code; }
  (void)exited;
  g_exit = nullptr;
  res.ran = true; res.output = out;
  munmap(mem, len);
  return res;
#endif
}
}
