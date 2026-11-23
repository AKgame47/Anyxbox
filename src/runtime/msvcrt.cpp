#include "runtime/msvcrt.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__linux__) && defined(__x86_64__)
#define MSABI __attribute__((ms_abi))
namespace anyxbox {
namespace {
MsvcrtContext* C = nullptr;
MSABI void* m_memcpy(void* d, const void* s, uint64_t n) { return n ? std::memcpy(d, s, n) : d; }
MSABI void* m_memmove(void* d, const void* s, uint64_t n) { return n ? std::memmove(d, s, n) : d; }
MSABI void* m_memset(void* d, int c, uint64_t n) { return n ? std::memset(d, c, n) : d; }
MSABI int m_memcmp(const void* a, const void* b, uint64_t n) { return n ? std::memcmp(a, b, n) : 0; }
MSABI uint64_t m_strlen(const char* s) { return std::strlen(s); }
MSABI int m_strcmp(const char* a, const char* b) { return std::strcmp(a, b); }
MSABI int m_strncmp(const char* a, const char* b, uint64_t n) { return std::strncmp(a, b, n); }
MSABI int m_abs(int v) { return v < 0 ? -v : v; }

void* do_alloc(size_t n, bool zero) {
  if (n == 0) n = 1;
  std::lock_guard<std::mutex> l(C->m);
  if (n > C->heap_limit || C->heap_used > C->heap_limit - n) return nullptr;
  void* p = zero ? std::calloc(1, n) : std::malloc(n);
  if (!p) return nullptr;
  C->heap[(uintptr_t)p] = n; C->heap_used += n;
  return p;
}
MSABI void* m_malloc(uint64_t n) { return do_alloc((size_t)n, false); }
MSABI void* m_calloc(uint64_t a, uint64_t b) {
  if (a && b > (size_t(1) << 40) / a) return nullptr;
  return do_alloc((size_t)(a * b), true);
}
MSABI void m_free(void* p) {
  if (!p) return;
  std::lock_guard<std::mutex> l(C->m);
  auto it = C->heap.find((uintptr_t)p);
  if (it == C->heap.end()) return;            // unknown or double free: ignored, never forwarded to the host
  C->heap_used -= it->second; C->heap.erase(it); std::free(p);
}
MSABI void* m_realloc(void* p, uint64_t n) {
  if (!p) return do_alloc((size_t)n, false);
  size_t old;
  { std::lock_guard<std::mutex> l(C->m); auto it = C->heap.find((uintptr_t)p); if (it == C->heap.end()) return nullptr; old = it->second; }
  if (n == 0) { m_free(p); return nullptr; }
  void* q = do_alloc((size_t)n, false);
  if (!q) return nullptr;
  std::memcpy(q, p, old < n ? old : (size_t)n);
  m_free(p);
  return q;
}
MSABI int m_puts(const char* s) { if (!s) return -1; if (C->out) { C->out->append(s); C->out->push_back('\n'); } return 1; }
MSABI int m_putchar(int c) { if (C->out) C->out->push_back((char)c); return c; }

// printf-lite over a Windows-ABI va_list
std::string format(const char* f, __builtin_ms_va_list ap) {
  std::string o;
  for (; *f; f++) {
    if (*f != '%') { o.push_back(*f); continue; }
    f++;
    bool left = false, zero = false;
    for (; *f == '-' || *f == '0'; f++) (*f == '-' ? left : zero) = true;
    int width = 0; while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); if (width > 4096) width = 4096; f++; }
    int lng = 0; while (*f == 'l' || *f == 'z' || *f == 'h' || *f == 'I') { if (*f == 'l' || *f == 'z' || *f == 'I') lng++; f++; }
    // note: on Windows x64 long is 32-bit, so only ll/z/I64 mean 64-bit
    bool is64 = lng >= 2;
    std::string body; char t[40];
    switch (*f) {
      case 'd': case 'i': { long long v = is64 ? __builtin_va_arg(ap, long long) : (long long)__builtin_va_arg(ap, int); std::snprintf(t, sizeof t, "%lld", v); body = t; break; }
      case 'u': { unsigned long long v = is64 ? __builtin_va_arg(ap, unsigned long long) : (unsigned long long)__builtin_va_arg(ap, unsigned); std::snprintf(t, sizeof t, "%llu", v); body = t; break; }
      case 'x': case 'X': { unsigned long long v = is64 ? __builtin_va_arg(ap, unsigned long long) : (unsigned long long)__builtin_va_arg(ap, unsigned); std::snprintf(t, sizeof t, *f == 'x' ? "%llx" : "%llX", v); body = t; break; }
      case 'p': { void* v = __builtin_va_arg(ap, void*); std::snprintf(t, sizeof t, "%p", v); body = t; break; }
      case 'c': { int v = __builtin_va_arg(ap, int); body.assign(1, (char)v); break; }
      case 's': { const char* v = __builtin_va_arg(ap, const char*); body = v ? v : "(null)"; break; }
      case '%': body = "%"; break;
      case 0: return o;
      default: body = std::string("%") + *f; break;
    }
    if ((int)body.size() < width) {
      size_t pad = width - body.size();
      if (left) body.append(pad, ' ');
      else if (zero && *f != 's' && *f != 'c') body.insert(body[0] == '-' ? 1 : 0, pad, '0');
      else body.insert(0, pad, ' ');
    }
    o += body;
  }
  return o;
}
MSABI int m_printf(const char* f, ...) {
  if (!f) return -1;
  __builtin_ms_va_list ap; __builtin_ms_va_start(ap, f);
  std::string s = format(f, ap);
  __builtin_ms_va_end(ap);
  if (C->out) C->out->append(s);
  return (int)s.size();
}
}  // namespace

bool msvcrt_register(ImportRegistry& r, MsvcrtContext& ctx) {
  C = &ctx;
  auto A = [](auto* f) { return (uint64_t)(uintptr_t)f; };
  for (const char* d : {"msvcrt.dll", "ucrtbase.dll", "vcruntime140.dll"}) {
    r.add(d, "memcpy", A(&m_memcpy)); r.add(d, "memmove", A(&m_memmove)); r.add(d, "memset", A(&m_memset));
    r.add(d, "memcmp", A(&m_memcmp)); r.add(d, "strlen", A(&m_strlen)); r.add(d, "strcmp", A(&m_strcmp));
    r.add(d, "strncmp", A(&m_strncmp)); r.add(d, "abs", A(&m_abs));
    r.add(d, "malloc", A(&m_malloc)); r.add(d, "calloc", A(&m_calloc)); r.add(d, "realloc", A(&m_realloc));
    r.add(d, "free", A(&m_free)); r.add(d, "puts", A(&m_puts)); r.add(d, "putchar", A(&m_putchar));
    r.add(d, "printf", A(&m_printf));
  }
  return true;
}
}
#else
namespace anyxbox { bool msvcrt_register(ImportRegistry&, MsvcrtContext&) { return false; } }
#endif
