#include "runtime/kernel32.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#define MSABI __attribute__((ms_abi))
namespace anyxbox {
namespace {
Kernel32Context* C = nullptr;
const uint64_t kStdIn = 0xFFF0, kStdOut = 0xFFF4, kStdErr = 0xFFF8, kInvalid = ~0ull;
const uint32_t GENERIC_READ_ = 0x80000000u, GENERIC_WRITE_ = 0x40000000u;
bool h32(uint64_t h, uint32_t& o) { if (h > 0xFFFFFFFFull) return false; o = (uint32_t)h; return true; }
bool is_std_out(uint64_t h) { return h == kStdOut || h == kStdErr; }

MSABI uint64_t k_GetStdHandle(uint32_t n) {
  if (n == (uint32_t)-10) return kStdIn;
  if (n == (uint32_t)-11) return kStdOut;
  if (n == (uint32_t)-12) return kStdErr;
  return kInvalid;
}
MSABI uint32_t k_WriteFile(uint64_t h, const void* buf, uint32_t n, uint32_t* written, void* ov) {
  if (ov || (!buf && n)) return 0;
  if (is_std_out(h)) { if (C->out) C->out->append((const char*)buf, n); if (written) *written = n; return 1; }
  uint32_t hh; if (!h32(h, hh)) return 0;
  uint32_t w = 0; bool ok = anyxbox::WriteFile(C->handles, hh, buf, n, &w);
  if (written) *written = w;
  return ok ? 1 : 0;
}
MSABI uint32_t k_ReadFile(uint64_t h, void* buf, uint32_t n, uint32_t* got, void* ov) {
  if (ov || (!buf && n)) return 0;
  uint32_t hh; if (!h32(h, hh)) return 0;
  uint32_t g = 0; bool ok = anyxbox::ReadFile(C->handles, hh, buf, n, &g);
  if (got) *got = g;
  return ok ? 1 : 0;
}
MSABI uint64_t k_CreateFileA(const char* name, uint32_t access, uint32_t, void*, uint32_t disp, uint32_t, void*) {
  if (!name || !C->folder) return kInvalid;
  bool wr = (access & GENERIC_WRITE_) != 0;
  FileMode m;
  if (disp == 2 && wr) m = CREATE_WRITE_TRUNCATE;        // CREATE_ALWAYS
  else if (disp == 3 && !wr && (access & GENERIC_READ_)) m = OPEN_EXISTING_RW_READ;  // OPEN_EXISTING, read
  else if (disp == 4 && wr) m = OPEN_APPEND;             // OPEN_ALWAYS, write (append)
  else return kInvalid;
  uint32_t h = C->folder->CreateFile(C->handles, name, m);
  return h == 0xFFFFFFFF ? kInvalid : h;
}
MSABI uint32_t k_CloseHandle(uint64_t h) { uint32_t hh; return h32(h, hh) && C->handles.close(hh) ? 1 : 0; }
MSABI uint32_t k_SetFilePointer(uint64_t h, int32_t lo, int32_t* hi, uint32_t method) {
  uint32_t hh; uint64_t np = 0;
  int64_t off = lo; if (hi) off |= (int64_t)*hi << 32;
  if (!h32(h, hh) || method > 2 || !anyxbox::SetFilePointer(C->handles, hh, off, (int)method, &np)) return 0xFFFFFFFFu;
  if (hi) *hi = (int32_t)(np >> 32);
  return (uint32_t)np;
}
MSABI uint32_t k_GetFileSize(uint64_t h, uint32_t* hi) {
  uint32_t hh; uint64_t sz = 0;
  if (!h32(h, hh) || !anyxbox::GetFileSize(C->handles, hh, &sz)) return 0xFFFFFFFFu;
  if (hi) *hi = (uint32_t)(sz >> 32);
  return (uint32_t)sz;
}
MSABI uint64_t k_GetModuleHandleA(const char* name) { return name ? 0 : C->image_base; }
MSABI void* k_VirtualAlloc(void* addr, uint64_t size, uint32_t, uint32_t) { return addr ? nullptr : C->vm.alloc((size_t)size); }
MSABI uint32_t k_VirtualFree(void* addr, uint64_t size, uint32_t type) {
  if (type != 0x8000 || size != 0) return 0;   // MEM_RELEASE only
  return C->vm.free(addr) ? 1 : 0;
}
typedef uint32_t (MSABI *ThreadStart)(void*);
MSABI uint64_t k_CreateThread(void*, uint64_t, ThreadStart fn, void* param, uint32_t, uint32_t* tid) {
  if (!fn) return 0;
  uint32_t h = anyxbox::CreateThread(C->handles, [fn, param] { return fn(param); });
  if (tid && h) *tid = h;
  return h;
}
MSABI uint32_t k_GetExitCodeThread(uint64_t h, uint32_t* code) { uint32_t hh; return h32(h, hh) && anyxbox::GetExitCodeThread(C->handles, hh, code) ? 1 : 0; }
MSABI uint32_t k_WaitForSingleObject(uint64_t h, uint32_t ms) { uint32_t hh; return h32(h, hh) ? anyxbox::WaitForSingleObject(C->handles, hh, ms) : WAIT_FAILED; }
MSABI uint64_t k_CreateEventA(void*, int32_t manual, int32_t initial, const char*) { return C->handles.add(std::make_shared<Event>(manual != 0, initial != 0)); }
MSABI uint32_t k_SetEvent(uint64_t h) { uint32_t hh; if (!h32(h, hh)) return 0; auto e = std::dynamic_pointer_cast<Event>(C->handles.get(hh)); if (!e) return 0; e->set(); return 1; }
MSABI uint32_t k_ResetEvent(uint64_t h) { uint32_t hh; if (!h32(h, hh)) return 0; auto e = std::dynamic_pointer_cast<Event>(C->handles.get(hh)); if (!e) return 0; e->reset(); return 1; }
MSABI void k_Sleep(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
MSABI uint64_t k_GetTickCount64() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
MSABI uint32_t k_QueryPerformanceCounter(int64_t* v) {
  if (!v) return 0;
  *v = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  return 1;
}
MSABI uint32_t k_QueryPerformanceFrequency(int64_t* v) { if (!v) return 0; *v = 1000000000; return 1; }

CriticalSection* find_cs(void* p) {
  std::lock_guard<std::mutex> l(C->cs_mutex);
  auto it = C->critical_sections.find((uintptr_t)p);
  return it == C->critical_sections.end() ? nullptr : it->second.get();
}
MSABI void k_InitializeCriticalSection(void* p) {
  if (!p) return;
  std::lock_guard<std::mutex> l(C->cs_mutex);
  C->critical_sections[(uintptr_t)p] = std::make_unique<CriticalSection>();
}
MSABI void k_EnterCriticalSection(void* p) { if (auto* c = find_cs(p)) c->enter(); }
MSABI uint32_t k_TryEnterCriticalSection(void* p) { auto* c = find_cs(p); return c && c->try_enter() ? 1 : 0; }
MSABI void k_LeaveCriticalSection(void* p) { if (auto* c = find_cs(p)) c->leave(); }
MSABI void k_DeleteCriticalSection(void* p) { std::lock_guard<std::mutex> l(C->cs_mutex); C->critical_sections.erase((uintptr_t)p); }

// ---- process/thread basics, TLS, a small process heap, time (what a C runtime startup typically touches) ----
thread_local uint32_t t_last_error = 0;
const int kTlsSlots = 64;
bool g_tls_used[kTlsSlots];
thread_local uint64_t t_tls[kTlsSlots];
std::mutex g_tls_mutex, g_heap_mutex;
std::map<uintptr_t, size_t> g_heap_blocks;
size_t g_heap_used = 0;
const size_t kHeapLimit = size_t(1) << 30;
MSABI uint32_t k_GetLastError() { return t_last_error; }
MSABI void k_SetLastError(uint32_t e) { t_last_error = e; }
MSABI uint32_t k_GetCurrentProcessId() { return 4242; }
MSABI uint32_t k_GetCurrentThreadId() { return (uint32_t)(std::hash<std::thread::id>()(std::this_thread::get_id()) & 0x7FFFFFFFu) | 1u; }
MSABI uint64_t k_GetCurrentProcess() { return ~0ull; }
MSABI uint64_t k_GetCurrentThread() { return ~1ull; }
MSABI uint32_t k_GetTickCount() { return (uint32_t)k_GetTickCount64(); }
MSABI void k_GetSystemTimeAsFileTime(uint64_t* ft) {
  if (!ft) return;
  auto d = std::chrono::system_clock::now().time_since_epoch();
  *ft = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(d).count() / 100 + 116444736000000000ull;
}
MSABI uint32_t k_IsDebuggerPresent() { return 0; }
MSABI uint64_t k_SetUnhandledExceptionFilter(uint64_t) { return 0; }
MSABI uint32_t k_TlsAlloc() {
  std::lock_guard<std::mutex> l(g_tls_mutex);
  for (int i = 0; i < kTlsSlots; i++) if (!g_tls_used[i]) { g_tls_used[i] = true; t_tls[i] = 0; return (uint32_t)i; }
  return 0xFFFFFFFFu;
}
MSABI uint32_t k_TlsFree(uint32_t i) {
  std::lock_guard<std::mutex> l(g_tls_mutex);
  if (i >= (uint32_t)kTlsSlots || !g_tls_used[i]) return 0;
  g_tls_used[i] = false; return 1;
}
MSABI uint64_t k_TlsGetValue(uint32_t i) { t_last_error = 0; return i < (uint32_t)kTlsSlots && g_tls_used[i] ? t_tls[i] : 0; }
MSABI uint32_t k_TlsSetValue(uint32_t i, uint64_t v) {
  if (i >= (uint32_t)kTlsSlots || !g_tls_used[i]) return 0;
  t_tls[i] = v; return 1;
}
MSABI const char* k_GetCommandLineA() { return "anyxbox_program"; }
MSABI uint64_t k_GetProcessHeap() { return 0x9000; }
MSABI void* k_HeapAlloc(uint64_t heap, uint32_t flags, uint64_t n) {
  if (heap != 0x9000) return nullptr;
  if (n == 0) n = 1;
  std::lock_guard<std::mutex> l(g_heap_mutex);
  if (n > kHeapLimit || g_heap_used > kHeapLimit - n) return nullptr;
  void* p = (flags & 8) ? std::calloc(1, n) : std::malloc(n);   // 8 = HEAP_ZERO_MEMORY
  if (!p) return nullptr;
  g_heap_blocks[(uintptr_t)p] = n; g_heap_used += n; return p;
}
MSABI uint32_t k_HeapFree(uint64_t heap, uint32_t, void* p) {
  if (heap != 0x9000) return 0;
  if (!p) return 1;
  std::lock_guard<std::mutex> l(g_heap_mutex);
  auto it = g_heap_blocks.find((uintptr_t)p);
  if (it == g_heap_blocks.end()) return 0;   // unknown or double free: never forwarded to the host
  g_heap_used -= it->second; g_heap_blocks.erase(it); std::free(p); return 1;
}
MSABI void k_OutputDebugStringA(const char*) {}

// ---- startup helpers of a C runtime: STARTUPINFO, code pages, page protection queries ----
MSABI void k_GetStartupInfoA(uint8_t* si) { if (!si) return; std::memset(si, 0, 104); uint32_t cb = 104; std::memcpy(si, &cb, 4); }
MSABI uint32_t k_IsDBCSLeadByteEx(uint32_t, uint8_t) { return 0; }
MSABI int32_t k_MultiByteToWideChar(uint32_t, uint32_t, const uint8_t* src, int32_t n, uint16_t* dst, int32_t dn) {
  if (!src || dn < 0) { t_last_error = 87; return 0; }
  if (n < 0) { n = 0; while (src[n]) n++; n++; }                 // includes the terminating NUL
  if (dn == 0) return n;
  if (dn < n) { t_last_error = 122; return 0; }
  for (int32_t i = 0; i < n; i++) dst[i] = src[i];               // Latin-1 / ASCII only
  return n;
}
MSABI int32_t k_WideCharToMultiByte(uint32_t, uint32_t, const uint16_t* src, int32_t n, uint8_t* dst, int32_t dn, const char*, uint32_t* used) {
  if (!src || dn < 0) { t_last_error = 87; return 0; }
  if (n < 0) { n = 0; while (src[n]) n++; n++; }
  if (used) *used = 0;
  if (dn == 0) return n;
  if (dn < n) { t_last_error = 122; return 0; }
  for (int32_t i = 0; i < n; i++) { bool bad = src[i] > 0xFF; dst[i] = bad ? '?' : (uint8_t)src[i]; if (bad && used) *used = 1; }
  return n;
}
// Protection of the host page range containing addr, from /proc/self/maps. Returns false when unmapped.
bool host_region(uint64_t addr, uint64_t* base, uint64_t* size, uint32_t* prot) {
  FILE* f = std::fopen("/proc/self/maps", "r");
  if (!f) return false;
  char line[512]; bool found = false;
  while (std::fgets(line, sizeof line, f)) {
    unsigned long long lo, hi; char perms[8] = {};
    if (std::sscanf(line, "%llx-%llx %7s", &lo, &hi, perms) != 3) continue;
    if (addr >= lo && addr < hi) {
      *base = lo; *size = hi - lo;
      bool r = perms[0] == 'r', w = perms[1] == 'w', x = perms[2] == 'x';
      *prot = !r && !w && !x ? 1 : x ? (w ? 0x40 : r ? 0x20 : 0x10) : (w ? 4 : 2);
      found = true; break;
    }
  }
  std::fclose(f);
  return found;
}
bool addr_allowed(uint64_t addr, uint64_t len) {
  if (len == 0 || addr + len < addr) return false;
  if (C->image_base && addr >= C->image_base && addr + len <= C->image_base + C->image_size) return true;
  size_t rs = C->vm.region_size((const void*)addr);
  return rs != 0 && len <= rs;
}
MSABI uint64_t k_VirtualQuery(uint64_t addr, uint8_t* mbi, uint64_t len) {
  if (!mbi || len < 48 || !addr_allowed(addr, 1)) { t_last_error = 87; return 0; }
  uint64_t base, size; uint32_t prot;
  if (!host_region(addr, &base, &size, &prot)) { t_last_error = 87; return 0; }
  uint64_t pg = addr & ~uint64_t(4095);
  std::memset(mbi, 0, 48);
  uint32_t st = 0x1000, ty = 0x20000;
  std::memcpy(mbi, &pg, 8); std::memcpy(mbi + 8, &pg, 8); std::memcpy(mbi + 16, &prot, 4);
  uint64_t rs = 4096; std::memcpy(mbi + 24, &rs, 8); std::memcpy(mbi + 32, &st, 4); std::memcpy(mbi + 36, &prot, 4); std::memcpy(mbi + 40, &ty, 4);
  return 48;
}
MSABI uint32_t k_VirtualProtect(uint64_t addr, uint64_t size, uint32_t np, uint32_t* old) {
  if (!old || !addr_allowed(addr, size)) { t_last_error = 87; return 0; }
  int prot;
  switch (np & 0xFF) {
    case 1: prot = PROT_NONE; break; case 2: prot = PROT_READ; break; case 4: case 8: prot = PROT_READ | PROT_WRITE; break;
    case 0x10: prot = PROT_EXEC; break; case 0x20: prot = PROT_READ | PROT_EXEC; break;
    case 0x40: case 0x80: prot = PROT_READ | PROT_WRITE | PROT_EXEC; break;
    default: t_last_error = 87; return 0;
  }
  uint64_t b, sz; uint32_t cur;
  if (!host_region(addr, &b, &sz, &cur)) { t_last_error = 487; return 0; }
  uint64_t lo = addr & ~uint64_t(4095), hi = (addr + size + 4095) & ~uint64_t(4095);
  if (mprotect((void*)lo, hi - lo, prot) != 0) { t_last_error = 5; return 0; }
  *old = cur; return 1;
}
}  // namespace

bool kernel32_register(ImportRegistry& r, Kernel32Context& ctx) {
  C = &ctx;
  auto A = [](auto* f) { return (uint64_t)(uintptr_t)f; };
  const char* K = "kernel32.dll";
  r.add(K, "GetStdHandle", A(&k_GetStdHandle));
  r.add(K, "WriteFile", A(&k_WriteFile));
  r.add(K, "ReadFile", A(&k_ReadFile));
  r.add(K, "CreateFileA", A(&k_CreateFileA));
  r.add(K, "CloseHandle", A(&k_CloseHandle));
  r.add(K, "SetFilePointer", A(&k_SetFilePointer));
  r.add(K, "GetFileSize", A(&k_GetFileSize));
  r.add(K, "GetModuleHandleA", A(&k_GetModuleHandleA));
  r.add(K, "VirtualAlloc", A(&k_VirtualAlloc));
  r.add(K, "VirtualFree", A(&k_VirtualFree));
  r.add(K, "CreateThread", A(&k_CreateThread));
  r.add(K, "GetExitCodeThread", A(&k_GetExitCodeThread));
  r.add(K, "WaitForSingleObject", A(&k_WaitForSingleObject));
  r.add(K, "CreateEventA", A(&k_CreateEventA));
  r.add(K, "SetEvent", A(&k_SetEvent));
  r.add(K, "ResetEvent", A(&k_ResetEvent));
  r.add(K, "Sleep", A(&k_Sleep));
  r.add(K, "GetTickCount64", A(&k_GetTickCount64));
  r.add(K, "QueryPerformanceCounter", A(&k_QueryPerformanceCounter));
  r.add(K, "QueryPerformanceFrequency", A(&k_QueryPerformanceFrequency));
  r.add(K, "InitializeCriticalSection", A(&k_InitializeCriticalSection));
  r.add(K, "EnterCriticalSection", A(&k_EnterCriticalSection));
  r.add(K, "TryEnterCriticalSection", A(&k_TryEnterCriticalSection));
  r.add(K, "LeaveCriticalSection", A(&k_LeaveCriticalSection));
  r.add(K, "DeleteCriticalSection", A(&k_DeleteCriticalSection));
  r.add(K, "GetLastError", A(&k_GetLastError));
  r.add(K, "SetLastError", A(&k_SetLastError));
  r.add(K, "GetCurrentProcessId", A(&k_GetCurrentProcessId));
  r.add(K, "GetCurrentThreadId", A(&k_GetCurrentThreadId));
  r.add(K, "GetCurrentProcess", A(&k_GetCurrentProcess));
  r.add(K, "GetCurrentThread", A(&k_GetCurrentThread));
  r.add(K, "GetTickCount", A(&k_GetTickCount));
  r.add(K, "GetSystemTimeAsFileTime", A(&k_GetSystemTimeAsFileTime));
  r.add(K, "IsDebuggerPresent", A(&k_IsDebuggerPresent));
  r.add(K, "SetUnhandledExceptionFilter", A(&k_SetUnhandledExceptionFilter));
  r.add(K, "TlsAlloc", A(&k_TlsAlloc));
  r.add(K, "TlsFree", A(&k_TlsFree));
  r.add(K, "TlsGetValue", A(&k_TlsGetValue));
  r.add(K, "TlsSetValue", A(&k_TlsSetValue));
  r.add(K, "GetCommandLineA", A(&k_GetCommandLineA));
  r.add(K, "GetProcessHeap", A(&k_GetProcessHeap));
  r.add(K, "HeapAlloc", A(&k_HeapAlloc));
  r.add(K, "HeapFree", A(&k_HeapFree));
  r.add(K, "OutputDebugStringA", A(&k_OutputDebugStringA));
  r.add(K, "GetStartupInfoA", A(&k_GetStartupInfoA));
  r.add(K, "IsDBCSLeadByteEx", A(&k_IsDBCSLeadByteEx));
  r.add(K, "MultiByteToWideChar", A(&k_MultiByteToWideChar));
  r.add(K, "WideCharToMultiByte", A(&k_WideCharToMultiByte));
  r.add(K, "VirtualQuery", A(&k_VirtualQuery));
  r.add(K, "VirtualProtect", A(&k_VirtualProtect));
  return true;
}
}
#else
namespace anyxbox { bool kernel32_register(ImportRegistry&, Kernel32Context&) { return false; } }
#endif
