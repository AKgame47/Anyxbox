#include "runtime/kernel32.h"
#include <chrono>
#include <thread>
#if defined(__linux__) && defined(__x86_64__)
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
  return true;
}
}
#else
namespace anyxbox { bool kernel32_register(ImportRegistry&, Kernel32Context&) { return false; } }
#endif
