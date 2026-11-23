#include "runtime/run_pe.h"
#include <csetjmp>
#include <cstring>
#include "loader/pe_image.h"
#include "syslayer/import_registry.h"
#include "runtime/kernel32.h"
#include "runtime/msvcrt.h"
#include "runtime/devices.h"
#include "runtime/seh.h"
#include "runtime/teb.h"
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
typedef void (MSABI *TlsCallback)(void*, uint32_t, void*);
}  // namespace
#endif

RunResult run_pe(const uint8_t* data, size_t size, bool trust, const std::function<void(ImportRegistry&)>& extra_imports, const RunOptions& options) {
  RunResult res;
#if !ANYXBOX_CAN_RUN
  (void)data; (void)size; (void)trust; (void)options;
  res.error = "running programs is only supported on Linux x86-64 hosts for now";
  return res;
#else
  if (!trust) { res.error = "refusing to run: binary not marked as trusted"; return res; }
  std::unique_ptr<MappedFolder> assets;
  if(!options.asset_root.empty()) {
    assets=std::make_unique<MappedFolder>(options.asset_root,true);
    if(!assets->valid()) {res.error="asset root must be an existing non-symlink directory";return res;}
  }
  uint32_t isz = pe_image_size(data, size);
  if (!isz) { res.error = "invalid PE headers"; return res; }
  size_t len = ((size_t)isz + 4095) & ~size_t(4095);
  void* mem = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) { res.error = "mmap failed"; return res; }
  ImportRegistry reg;
  Kernel32Context k32;
  std::string out;
  k32.out = &out;
  k32.folder=std::move(assets);
  kernel32_register(reg, k32);
  MsvcrtContext crt;
  crt.out = &out;
  crt.exit_fn = [](uint32_t c) { host_ExitProcess(c); };
  msvcrt_register(reg, crt);
  seh_register(reg);
  seh_install_fault_handlers();
  XInputContext xi; xinput_register(reg, xi);       // no pad connected unless the host connects one
  XAudio2Context xa; xaudio2_register(reg, xa);     // plays into nothing
  if (extra_imports) extra_imports(reg);
  reg.add("kernel32.dll", "ExitProcess", (uint64_t)(uintptr_t)&host_ExitProcess);
  auto img = load_pe_image(data, size, [&](const std::string& d, const std::string& n, uint16_t o, bool b) {
    return reg.resolve(d, n, o, b); }, (uint8_t*)mem, len);
  res.unresolved = img.unresolved;
  if (!img.ok) { res.error = img.error; munmap(mem, len); return res; }
  // This launcher accepts executable entry points only. Check the declared section
  // extent, not rounded mprotect pages: padding and headers are not entry code.
  auto executable_rva=[&](uint64_t rva) {
    if(rva==0 || rva>=img.image_size)return false;
    for(const auto& sc:img.sections) {
      if((sc.flags & 0x20000000u) && rva>=sc.rva && rva-sc.rva<sc.size)return true;
    }
    return false;
  };
  if(!executable_rva(img.entry_rva)) {
    res.error="entry point is not within an executable section";
    munmap(mem,len);return res;
  }
  // Validate the entire list first. No valid earlier callback may execute before a
  // later invalid address is discovered. These addresses are also reused on threads.
  for(uint64_t callback:img.tls.callbacks) {
    if(callback<img.actual_base || !executable_rva(callback-img.actual_base)) {
      res.error="TLS callback is not within an executable section";
      munmap(mem,len);return res;
    }
  }
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
  k32.image_base = img.actual_base; k32.image_size = len;
  k32.tls_callbacks = img.tls.callbacks;
  g_exit = &jb; g_exit_code = 0;
  EntryFn fn = (EntryFn)(uintptr_t)(img.actual_base + img.entry_rva);
  volatile bool exited = false;
  seh_register_image(img.actual_base, len > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)len, img.pdata_rva, img.pdata_size);
  seh_set_unhandled([](uint32_t code) { host_ExitProcess(code); });   // no guest handler took the exception: the program ends with its code
  if (img.tls.present) teb_set_tls_template((const uint8_t*)(uintptr_t)img.tls.start_va, (uint32_t)(img.tls.end_va - img.tls.start_va), img.tls.zero_fill);
  teb_install(img.actual_base);
  if (setjmp(jb) == 0) {
    // TLS callbacks run with DLL_PROCESS_ATTACH before entry; CreateThread runs thread attach/detach callbacks.
    for (uint64_t cb : img.tls.callbacks) ((TlsCallback)(uintptr_t)cb)((void*)(uintptr_t)img.actual_base, 1, nullptr);
    res.exit_code = fn();
  } else { exited = true; res.exit_code = g_exit_code; }
  (void)exited;
  teb_restore();
  teb_set_tls_template(nullptr, 0, 0);
  seh_set_unhandled(nullptr);
  seh_unregister_image(img.actual_base);
  g_exit = nullptr;
  res.ran = true; res.output = out;
  munmap(mem, len);
  return res;
#endif
}
}
