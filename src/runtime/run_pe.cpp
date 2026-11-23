#include "runtime/run_pe.h"
#include <csetjmp>
#include <cstring>
#include "loader/pe_image.h"
#include "syslayer/import_registry.h"
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
thread_local std::string* g_out = nullptr;
thread_local std::jmp_buf* g_exit = nullptr;
thread_local uint32_t g_exit_code = 0;
const uint64_t kStdOut = 8, kStdErr = 12, kStdIn = 4, kInvalid = ~0ull;

MSABI uint64_t host_GetStdHandle(uint32_t n) {
  if (n == (uint32_t)-10) return kStdIn;
  if (n == (uint32_t)-11) return kStdOut;
  if (n == (uint32_t)-12) return kStdErr;
  return kInvalid;
}
MSABI uint32_t host_WriteFile(uint64_t h, const char* buf, uint32_t n, uint32_t* written, void* overlapped) {
  if ((h != kStdOut && h != kStdErr) || overlapped || !buf) return 0;
  if (g_out) g_out->append(buf, n);
  if (written) *written = n;
  return 1;
}
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
  reg.add("kernel32.dll", "GetStdHandle", (uint64_t)(uintptr_t)&host_GetStdHandle);
  reg.add("kernel32.dll", "WriteFile", (uint64_t)(uintptr_t)&host_WriteFile);
  reg.add("kernel32.dll", "ExitProcess", (uint64_t)(uintptr_t)&host_ExitProcess);
  auto img = load_pe_image(data, size, [&](const std::string& d, const std::string& n, uint16_t o, bool b) {
    return reg.resolve(d, n, o, b); }, (uint8_t*)mem, len);
  res.unresolved = img.unresolved;
  if (!img.ok) { res.error = img.error; munmap(mem, len); return res; }
  res.loaded = true;
  // W^X: the image is mapped read+write while patched, then switched to read+exec. Data sections
  // that the program writes to will need split protections in a later phase.
  if (mprotect(mem, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) { res.error = "mprotect failed"; munmap(mem, len); return res; }
  std::jmp_buf jb; std::string out;
  g_out = &out; g_exit = &jb; g_exit_code = 0;
  EntryFn fn = (EntryFn)(uintptr_t)(img.actual_base + img.entry_rva);
  volatile bool exited = false;
  if (setjmp(jb) == 0) { res.exit_code = fn(); } else { exited = true; res.exit_code = g_exit_code; }
  (void)exited;
  g_out = nullptr; g_exit = nullptr;
  res.ran = true; res.output = out;
  munmap(mem, len);
  return res;
#endif
}
}
