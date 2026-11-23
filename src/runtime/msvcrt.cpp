#include "runtime/msvcrt.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
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

// printf-lite over a Windows-ABI va_list. Each conversion is formatted by the host snprintf with the same flags,
// width, precision and an explicit length, so the text matches a normal C library. Windows x64: long is 32 bits.
std::string format(const char* f, __builtin_ms_va_list ap) {
  std::string o;
  for (; *f; f++) {
    if (*f != '%') { o.push_back(*f); continue; }
    f++;
    std::string flags; bool left = false;
    for (; *f == '-' || *f == '0' || *f == '+' || *f == ' ' || *f == '#'; f++) { flags.push_back(*f); if (*f == '-') left = true; }
    (void)left;
    int width = -1, prec = -1;
    if (*f == '*') { width = __builtin_va_arg(ap, int); f++; if (width < 0) { flags.push_back('-'); width = -width; } }
    else if (*f >= '0' && *f <= '9') { width = 0; while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); if (width > 4096) width = 4096; f++; } }
    if (width > 4096) width = 4096;
    if (*f == '.') {
      f++; prec = 0;
      if (*f == '*') { prec = __builtin_va_arg(ap, int); f++; }
      else while (*f >= '0' && *f <= '9') { prec = prec * 10 + (*f - '0'); if (prec > 4096) prec = 4096; f++; }
      if (prec > 4096) prec = 4096;
    }
    bool is64 = false;
    for (;; f++) {
      if (*f == 'l' && f[1] == 'l') { is64 = true; f++; }
      else if (*f == 'z' || *f == 'j' || *f == 't') is64 = true;
      else if (*f == 'I' && f[1] == '6' && f[2] == '4') { is64 = true; f += 2; }
      else if (*f == 'I' && f[1] == '3' && f[2] == '2') f += 2;
      else if (*f == 'I') is64 = true;
      else if (*f == 'l' || *f == 'h' || *f == 'L') {}
      else break;
    }
    std::string spec = "%" + flags;
    if (width >= 0) spec += std::to_string(width);
    if (prec >= 0) spec += "." + std::to_string(prec);
    char t[4400]; int n = 0;
    switch (*f) {
      case 'd': case 'i': { long long v = is64 ? __builtin_va_arg(ap, long long) : (long long)__builtin_va_arg(ap, int);
        n = std::snprintf(t, sizeof t, (spec + "lld").c_str(), v); break; }
      case 'u': case 'x': case 'X': case 'o': { unsigned long long v = is64 ? __builtin_va_arg(ap, unsigned long long) : (unsigned long long)__builtin_va_arg(ap, unsigned);
        n = std::snprintf(t, sizeof t, (spec + "ll" + *f).c_str(), v); break; }
      case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': { double v = __builtin_va_arg(ap, double);
        n = std::snprintf(t, sizeof t, (spec + *f).c_str(), v); break; }
      case 'p': { void* v = __builtin_va_arg(ap, void*); n = std::snprintf(t, sizeof t, (spec + "p").c_str(), v); break; }
      case 'c': { int v = __builtin_va_arg(ap, int); n = std::snprintf(t, sizeof t, (spec + "c").c_str(), v); break; }
      case 's': { const char* v = __builtin_va_arg(ap, const char*); if (!v) v = "(null)";
        size_t len = 0; while (v[len] && (prec < 0 || (int)len < prec) && len < 4096) len++;   // never read past a missing NUL
        std::string tmp(v, len); std::string sp = "%" + flags + (width >= 0 ? std::to_string(width) : "") + "s";
        n = std::snprintf(t, sizeof t, sp.c_str(), tmp.c_str()); break; }
      case '%': t[0] = '%'; n = 1; break;
      case 0: return o;
      default: n = std::snprintf(t, sizeof t, "%%%c", *f); break;
    }
    if (n > 0) o.append(t, (size_t)(n < (int)sizeof t ? n : (int)sizeof t - 1));
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

// ---- C runtime startup set ----
alignas(16) uint8_t g_iob[3][48];                   // FILE objects of stdin, stdout, stderr (opaque, identified by address)
int g_fmode = 0, g_commode = 0;
char g_cmdline[] = "anyxbox_program";
char* g_argv[2] = {g_cmdline, nullptr};
char* g_envp[1] = {nullptr};
char** g_initenv = g_envp;
char* g_acmdln = g_cmdline;
thread_local int t_errno = 0;
std::vector<void (MSABI *)()> g_atexit;
std::recursive_mutex g_locks[64];
bool is_out(const void* f) { return f == &g_iob[1] || f == &g_iob[2]; }
[[noreturn]] void do_exit(uint32_t code) {
  C->exited = true; C->exit_code = code;
  if (C->exit_fn) C->exit_fn(code);
  __builtin_trap();                                    // no exit hook installed: a guest must not continue past exit
}
void run_atexit() { while (!g_atexit.empty()) { auto fn = g_atexit.back(); g_atexit.pop_back(); fn(); } }
MSABI int m___getmainargs(int* argc, char*** argv, char*** env, int, void*) {
  if (!argc || !argv || !env) return -1;
  *argc = 1; *argv = g_argv; *env = g_envp; return 0;
}
MSABI void m___set_app_type(int) {}
MSABI void* m___setusermatherr(void*) { return nullptr; }
MSABI void m__initterm(void (MSABI **b)(), void (MSABI **e)()) { for (; b && b < e; b++) if (*b) (*b)(); }
MSABI void* m__onexit(void* fn) { if (!fn || g_atexit.size() >= 256) return nullptr; g_atexit.push_back((void (MSABI *)())fn); return fn; }
MSABI void m__cexit() { run_atexit(); }
MSABI void m_exit(int code) { run_atexit(); do_exit((uint32_t)code); }
MSABI void m_abort() { do_exit(3); }
MSABI void m__amsg_exit(int) { do_exit(255); }
MSABI void* m_signal(int, void*) { return nullptr; }          // SIG_DFL; no signals are ever delivered
MSABI void m__lock(int n) { if (n >= 0 && n < 64) g_locks[n].lock(); }
MSABI void m__unlock(int n) { if (n >= 0 && n < 64) g_locks[n].unlock(); }
MSABI int* m__errno() { return &t_errno; }
MSABI const char* m_strerror(int) { return "Unknown error"; }
MSABI void* m_localeconv() {
  static char dot[] = ".", empty[] = "";
  static void* lc[16];
  lc[0] = dot; for (int i = 1; i < 10; i++) lc[i] = empty;       // decimal_point, the other strings empty (C locale)
  return lc;
}
MSABI int m___lconv_init() { return 0; }
MSABI uint32_t m____lc_codepage_func() { return 0; }
MSABI int m____mb_cur_max_func() { return 1; }
MSABI uint64_t m_wcslen(const uint16_t* s) { uint64_t n = 0; while (s[n]) n++; return n; }
MSABI int m___C_specific_handler(void*, void*, void*, void*) { return 1; }   // ExceptionContinueSearch; no exceptions are raised
MSABI void* m___iob_func() { return g_iob; }
MSABI uint64_t m_fwrite(const void* b, uint64_t sz, uint64_t n, void* f) {
  if (!is_out(f) || !b) return 0;
  if (sz && n > (size_t(1) << 32) / sz) return 0;
  if (C->out) C->out->append((const char*)b, (size_t)(sz * n));
  return n;
}
MSABI int m_fputc(int c, void* f) { if (!is_out(f)) return -1; if (C->out) C->out->push_back((char)c); return c & 0xFF; }
MSABI void* m_memchr(const void* p, int c, uint64_t n) { return n ? const_cast<void*>(std::memchr(p, c, (size_t)n)) : nullptr; }
MSABI int m_fputs(const char* s, void* f) { if (!s || !is_out(f)) return -1; if (C->out) C->out->append(s); return 1; }
// low-level file descriptors: only 0 (stdin, always at end of file), 1 and 2 (captured output) exist
MSABI int m__write(int fd, const void* b, unsigned n) { if ((fd != 1 && fd != 2) || (!b && n)) return -1; if (C->out && n) C->out->append((const char*)b, n); return (int)n; }
MSABI int m__read(int fd, void*, unsigned) { return fd == 0 ? 0 : -1; }
MSABI int m__close(int fd) { return fd >= 0 && fd <= 2 ? 0 : -1; }
MSABI int m_vfprintf(void* f, const char* fmt, __builtin_ms_va_list ap) {
  if (!is_out(f) || !fmt) return -1;
  std::string s = format(fmt, ap);
  if (C->out) C->out->append(s);
  return (int)s.size();
}
MSABI int m_fprintf(void* f, const char* fmt, ...) {
  if (!is_out(f) || !fmt) return -1;
  __builtin_ms_va_list ap; __builtin_ms_va_start(ap, fmt);
  std::string s = format(fmt, ap);
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
    r.add(d, "__getmainargs", A(&m___getmainargs)); r.add(d, "__set_app_type", A(&m___set_app_type)); r.add(d, "__setusermatherr", A(&m___setusermatherr));
    r.add(d, "_initterm", A(&m__initterm)); r.add(d, "_onexit", A(&m__onexit)); r.add(d, "_cexit", A(&m__cexit)); r.add(d, "exit", A(&m_exit));
    r.add(d, "abort", A(&m_abort)); r.add(d, "_amsg_exit", A(&m__amsg_exit)); r.add(d, "signal", A(&m_signal));
    r.add(d, "_lock", A(&m__lock)); r.add(d, "_unlock", A(&m__unlock)); r.add(d, "_errno", A(&m__errno)); r.add(d, "strerror", A(&m_strerror));
    r.add(d, "localeconv", A(&m_localeconv)); r.add(d, "__lconv_init", A(&m___lconv_init)); r.add(d, "___lc_codepage_func", A(&m____lc_codepage_func));
    r.add(d, "___mb_cur_max_func", A(&m____mb_cur_max_func)); r.add(d, "wcslen", A(&m_wcslen)); r.add(d, "__C_specific_handler", A(&m___C_specific_handler));
    r.add(d, "__iob_func", A(&m___iob_func)); r.add(d, "fwrite", A(&m_fwrite)); r.add(d, "fputc", A(&m_fputc));
    r.add(d, "memchr", A(&m_memchr)); r.add(d, "fputs", A(&m_fputs)); r.add(d, "_write", A(&m__write)); r.add(d, "_read", A(&m__read)); r.add(d, "_close", A(&m__close));
    r.add(d, "vfprintf", A(&m_vfprintf)); r.add(d, "fprintf", A(&m_fprintf));
    // data imports: the registry value is the address of the variable
    r.add(d, "_fmode", (uint64_t)(uintptr_t)&g_fmode); r.add(d, "_commode", (uint64_t)(uintptr_t)&g_commode);
    r.add(d, "_acmdln", (uint64_t)(uintptr_t)&g_acmdln); r.add(d, "__initenv", (uint64_t)(uintptr_t)&g_initenv);
  }
  return true;
}
}
#else
namespace anyxbox { bool msvcrt_register(ImportRegistry&, MsvcrtContext&) { return false; } }
#endif
