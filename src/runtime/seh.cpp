#include "runtime/seh.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ucontext.h>
#include <mutex>
#include <unistd.h>
#include <vector>
#if defined(__linux__) && defined(__x86_64__)
#define MSABI __attribute__((ms_abi))
extern "C" void __asan_handle_no_return(void) __attribute__((weak));
namespace anyxbox {
namespace {
// ---- CONTEXT (x64) field offsets ----
const size_t CTX_SIZE = 0x4D0, O_FLAGS = 0x30, O_GPR = 0x78, O_RSP = 0x98, O_RIP = 0xF8, O_XMM = 0x1A0;
uint64_t& R(uint8_t* c, int idx) { return *(uint64_t*)(c + O_GPR + 8 * idx); }   // 0 rax,1 rcx,2 rdx,3 rbx,4 rsp,5 rbp,6 rsi,7 rdi,8..15 r8..r15
uint64_t& RSP(uint8_t* c) { return *(uint64_t*)(c + O_RSP); }
uint64_t& RIP(uint8_t* c) { return *(uint64_t*)(c + O_RIP); }
struct RF { uint32_t begin, end, unwind; };
struct Img { uint64_t base; uint32_t size, pdata_rva, pdata_n; };
std::mutex g_mu;
std::vector<Img> g_imgs;
std::function<void(uint32_t)> g_unhandled;
struct Veh { uint64_t id, fn; bool cont; };
std::vector<Veh> g_veh;                 // vectored handlers in call order (first=1 ones at the front)
uint64_t g_veh_next = 1;
uint64_t g_filter = 0;                  // SetUnhandledExceptionFilter callback (guest pointer)
// Host frames (our dispatcher) have no unwind tables. When a walk reaches one, it continues from the newest saved continuation: the
// context the walk that called the handler was at. Entries whose stack lies below a landing point are dead and get pruned.
struct Cont { uint8_t c[0x4D0]; };
thread_local std::vector<Cont> t_cont;
void cont_prune(uint64_t rsp);
thread_local uint8_t* t_ctx0 = nullptr;   // context the current dispatch started from (host handlers unwind from here)
// ANYXBOX_SEH_TRACE=1 prints every dispatch step to stderr (diagnostics for CI logs).
bool trace() { static const bool t = std::getenv("ANYXBOX_SEH_TRACE") != nullptr; return t; }
const uint32_t EH_UNWINDING = 2, EH_EXIT_UNWIND = 4, EH_TARGET_UNWIND = 0x20;
struct DC { uint64_t ControlPc, ImageBase, FunctionEntry, EstablisherFrame, TargetIp, ContextRecord, LanguageHandler, HandlerData, HistoryTable; uint32_t ScopeIndex, Fill0; };
typedef int32_t (MSABI *Handler)(uint8_t* rec, uint64_t frame, uint8_t* ctx, DC* dc);

const RF* lookup(uint64_t pc, uint64_t* base_out) {
  std::lock_guard<std::mutex> l(g_mu);
  for (auto& im : g_imgs) {
    if (pc < im.base || pc >= im.base + im.size) continue;
    if (base_out) *base_out = im.base;
    const RF* t = (const RF*)(uintptr_t)(im.base + im.pdata_rva);
    uint32_t rva = (uint32_t)(pc - im.base), lo = 0, hi = im.pdata_n;
    while (lo < hi) { uint32_t mid = (lo + hi) / 2; if (rva < t[mid].begin) hi = mid; else if (rva >= t[mid].end) lo = mid + 1; else return &t[mid]; }
    return nullptr;
  }
  if (base_out) *base_out = 0;
  return nullptr;
}
bool in_image(uint64_t pc) {
  std::lock_guard<std::mutex> l(g_mu);
  for (auto& im : g_imgs) if (pc >= im.base && pc < im.base + im.size) return true;
  return false;
}
uint16_t u16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
uint32_t u32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint64_t u64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }

// Unwinds ctx by one frame (RtlVirtualUnwind). handler_type: 1 = EHANDLER, 2 = UHANDLER. Returns the language handler or 0.
uint64_t virtual_unwind(uint32_t handler_type, uint64_t base, uint64_t pc, const RF* rf, uint8_t* ctx, uint64_t* hdata, uint64_t* frame_out) {
  const uint8_t* ui = (const uint8_t*)(uintptr_t)(base + rf->unwind);
  uint32_t offset = (uint32_t)(pc - (base + rf->begin));
  uint32_t prolog = ui[1];
  int fr = ui[3] & 15; uint32_t fo = ui[3] >> 4;
  uint64_t establisher = fr ? R(ctx, fr) - (uint64_t)fo * 16 : RSP(ctx);
  if (frame_out) *frame_out = establisher;
  bool in_prolog = offset < prolog, machframe = false;
  for (int level = 0; level < 32; level++) {
    uint32_t count = ui[2]; const uint8_t* codes = ui + 4;
    for (uint32_t i = 0; i < count;) {
      uint8_t co = codes[2 * i], op = codes[2 * i + 1] & 15, info = codes[2 * i + 1] >> 4;
      bool apply = !(in_prolog && level == 0 && co > offset);
      uint32_t slots = 1;
      uint64_t& sp = RSP(ctx);
      switch (op) {
        case 0: if (apply) { R(ctx, info) = u64((const uint8_t*)(uintptr_t)sp); sp += 8; } break;
        case 1: { uint64_t sz; if (info == 0) { slots = 2; sz = (uint64_t)u16(codes + 2 * (i + 1)) * 8; } else { slots = 3; sz = u32(codes + 2 * (i + 1)); } if (apply) sp += sz; break; }
        case 2: if (apply) sp += (uint64_t)info * 8 + 8; break;
        case 3: if (apply) sp = establisher; break;
        case 4: slots = 2; if (apply) R(ctx, info) = u64((const uint8_t*)(uintptr_t)(sp + (uint64_t)u16(codes + 2 * (i + 1)) * 8)); break;
        case 5: slots = 3; if (apply) R(ctx, info) = u64((const uint8_t*)(uintptr_t)(sp + u32(codes + 2 * (i + 1)))); break;
        case 6: slots = 2; break;
        case 7: slots = 3; break;
        case 8: slots = 2; if (apply) std::memcpy(ctx + O_XMM + 16 * info, (const void*)(uintptr_t)(sp + (uint64_t)u16(codes + 2 * (i + 1)) * 16), 16); break;
        case 9: slots = 3; if (apply) std::memcpy(ctx + O_XMM + 16 * info, (const void*)(uintptr_t)(sp + u32(codes + 2 * (i + 1))), 16); break;
        case 10: if (apply) { if (info) sp += 8; RIP(ctx) = u64((const uint8_t*)(uintptr_t)sp); sp = u64((const uint8_t*)(uintptr_t)(sp + 24)); machframe = true; } break;
        default: return 0;   // unknown unwind code: stop here
      }
      i += slots;
    }
    uint8_t flags = ui[0] >> 3;
    const uint8_t* after = ui + 4 + ((count + 1) & ~1u) * 2;
    if (flags & 4) {   // chained: the parent's codes also apply, all of them
      const RF* ch = (const RF*)after;
      ui = (const uint8_t*)(uintptr_t)(base + ch->unwind);
      continue;
    }
    if (!machframe) { uint64_t& sp = RSP(ctx); RIP(ctx) = u64((const uint8_t*)(uintptr_t)sp); sp += 8; }
    if (flags & handler_type & 3) { if (hdata) *hdata = (uint64_t)(uintptr_t)(after + 4); return base + u32(after); }
    return 0;
  }
  return 0;
}
// leaf function without table entry: the return address is on top of the stack
void leaf_unwind(uint8_t* ctx) { uint64_t& sp = RSP(ctx); RIP(ctx) = u64((const uint8_t*)(uintptr_t)sp); sp += 8; }
}  // namespace
}
extern "C" void anyxbox_seh_restore(uint8_t* ctx);
asm(R"(
.text
.globl anyxbox_seh_restore
anyxbox_seh_restore:
  .intel_syntax noprefix
  mov r10, rdi
  movups xmm6, [r10+0x1A0+96]
  movups xmm7, [r10+0x1A0+112]
  movups xmm8, [r10+0x1A0+128]
  movups xmm9, [r10+0x1A0+144]
  movups xmm10, [r10+0x1A0+160]
  movups xmm11, [r10+0x1A0+176]
  movups xmm12, [r10+0x1A0+192]
  movups xmm13, [r10+0x1A0+208]
  movups xmm14, [r10+0x1A0+224]
  movups xmm15, [r10+0x1A0+240]
  mov rax, [r10+0x78]
  mov rcx, [r10+0x80]
  mov rdx, [r10+0x88]
  mov rbx, [r10+0x90]
  mov rbp, [r10+0xA0]
  mov rsi, [r10+0xA8]
  mov rdi, [r10+0xB0]
  mov r8,  [r10+0xB8]
  mov r9,  [r10+0xC0]
  mov r12, [r10+0xD8]
  mov r13, [r10+0xE0]
  mov r14, [r10+0xE8]
  mov r15, [r10+0xF0]
  mov r11, [r10+0xF8]
  mov rsp, [r10+0x98]
  jmp r11
  .att_syntax prefix
)");
// Entry stubs: save the guest's callee-saved registers, rsp and return address into a CONTEXT, then call the C++ implementation
// as impl(a1, a2, a3, a4, a5, a6, context). a5 and a6 are the guest's stack arguments 5 and 6.
#define ANYXBOX_SEH_STUB(NAME, IMPL) \
asm(".text\n.globl " NAME "\n" NAME ":\n.intel_syntax noprefix\n" \
  "push rbp\nmov rbp, rsp\nand rsp, -16\nsub rsp, 0x510\n" \
  "mov [rsp+0x40+0x78], rax\nmov [rsp+0x40+0x90], rbx\nmov rax, [rbp]\nmov [rsp+0x40+0xA0], rax\nmov [rsp+0x40+0xA8], rsi\nmov [rsp+0x40+0xB0], rdi\n" \
  "mov [rsp+0x40+0xD8], r12\nmov [rsp+0x40+0xE0], r13\nmov [rsp+0x40+0xE8], r14\nmov [rsp+0x40+0xF0], r15\n" \
  "lea rax, [rbp+0x10]\nmov [rsp+0x40+0x98], rax\nmov rax, [rbp+8]\nmov [rsp+0x40+0xF8], rax\n" \
  "movups [rsp+0x40+0x1A0+96], xmm6\nmovups [rsp+0x40+0x1A0+112], xmm7\nmovups [rsp+0x40+0x1A0+128], xmm8\nmovups [rsp+0x40+0x1A0+144], xmm9\n" \
  "movups [rsp+0x40+0x1A0+160], xmm10\nmovups [rsp+0x40+0x1A0+176], xmm11\nmovups [rsp+0x40+0x1A0+192], xmm12\nmovups [rsp+0x40+0x1A0+208], xmm13\n" \
  "movups [rsp+0x40+0x1A0+224], xmm14\nmovups [rsp+0x40+0x1A0+240], xmm15\n" \
  "mov dword ptr [rsp+0x40+0x30], 0x10001F\n" \
  "mov rax, [rbp+0x30]\nmov [rsp+0x20], rax\nmov rax, [rbp+0x38]\nmov [rsp+0x28], rax\n" \
  "lea rax, [rsp+0x40]\nmov [rsp+0x30], rax\n" \
  "call " IMPL "@PLT\n" \
  "mov rsp, rbp\npop rbp\nret\n.att_syntax prefix\n");
ANYXBOX_SEH_STUB("anyxbox_seh_RaiseException", "anyxbox_seh_impl_RaiseException")
ANYXBOX_SEH_STUB("anyxbox_seh_RtlCaptureContext", "anyxbox_seh_impl_RtlCaptureContext")
ANYXBOX_SEH_STUB("anyxbox_seh_RtlUnwindEx", "anyxbox_seh_impl_RtlUnwindEx")
ANYXBOX_SEH_STUB("anyxbox_seh_RtlUnwind", "anyxbox_seh_impl_RtlUnwind")
extern "C" {
void anyxbox_seh_RaiseException();
void anyxbox_seh_RtlCaptureContext();
void anyxbox_seh_RtlUnwindEx();
void anyxbox_seh_RtlUnwind();
}
namespace anyxbox {
namespace {
void cont_prune(uint64_t rsp) { while (!t_cont.empty() && RSP(t_cont.back().c) < rsp) t_cont.pop_back(); }
void cont_push(const uint8_t* c) { Cont k; std::memcpy(k.c, c, CTX_SIZE); t_cont.push_back(k); }
// Phase 1: walk the frames from ctx0 and offer the exception to each frame's handler. True when a handler chose to continue execution.
bool dispatch(uint8_t* rec, uint8_t* ctx0) {
  if (trace()) std::fprintf(stderr, "seh: dispatch code=%08x flags=%x rip=%llx rsp=%llx\n", u32(rec), u32(rec + 4), (unsigned long long)RIP(ctx0), (unsigned long long)RSP(ctx0));
  uint8_t* saved = t_ctx0; t_ctx0 = ctx0;
  cont_prune(RSP(ctx0));
  size_t base_depth = t_cont.size();
  cont_push(ctx0);
  long idx = (long)base_depth - 1;
  uint8_t c[CTX_SIZE], c2[CTX_SIZE];
  std::memcpy(c, ctx0, CTX_SIZE);
  bool continued = false;
  for (int depth = 0; depth < 512; depth++) {
    uint64_t pc = RIP(c), base = 0;
    const RF* rf = lookup(pc, &base);
    if (!rf) {
      if (trace()) std::fprintf(stderr, "seh:  no function entry for pc=%llx (%s)\n", (unsigned long long)pc, in_image(pc) ? "leaf" : "host frame");
      if (!in_image(pc)) { if (idx >= 0) { std::memcpy(c, t_cont[idx--].c, CTX_SIZE); continue; } break; }
      leaf_unwind(c); continue;
    }
    std::memcpy(c2, c, CTX_SIZE);
    uint64_t hdata = 0, frame = 0;
    uint64_t h = virtual_unwind(1, base, pc, rf, c2, &hdata, &frame);
    if (trace()) std::fprintf(stderr, "seh:  frame pc=%llx rva=%llx frame=%llx handler=%llx\n", (unsigned long long)pc, (unsigned long long)(pc - base), (unsigned long long)frame, (unsigned long long)(h ? h - base : 0));
    if (h) {
      DC dc{pc, base, (uint64_t)(uintptr_t)rf, frame, 0, (uint64_t)(uintptr_t)c, h, hdata, 0, 0, 0};
      int32_t disp = ((Handler)(uintptr_t)h)(rec, frame, c, &dc);
      if (trace()) std::fprintf(stderr, "seh:   disposition %d\n", disp);
      if (disp == 0) { continued = true; break; }
    }
    std::memcpy(c, c2, CTX_SIZE);
  }
  t_ctx0 = saved;
  t_cont.resize(base_depth);
  return continued;
}
// Vectored handlers, then frame handlers, then the unhandled-exception filter. True when execution should continue at ctx0
// (a vectored handler or the filter said EXCEPTION_CONTINUE_EXECUTION, or a frame handler did); false when nothing took it
// (the end-of-program callback has run).
typedef int32_t (MSABI *Veh_fn)(uint64_t* ptrs);
bool run_veh(uint8_t* rec, uint8_t* ctx, bool cont_list) {
  std::vector<Veh> list; { std::lock_guard<std::mutex> l(g_mu); list = g_veh; }
  uint64_t ptrs[2] = {(uint64_t)(uintptr_t)rec, (uint64_t)(uintptr_t)ctx};
  for (auto& v : list) {
    if (v.cont != cont_list) continue;
    if (trace()) std::fprintf(stderr, "seh: %s handler %llx\n", cont_list ? "continue" : "vectored", (unsigned long long)v.fn);
    if (((Veh_fn)(uintptr_t)v.fn)(ptrs) == -1) return true;   // EXCEPTION_CONTINUE_EXECUTION
  }
  return false;
}
bool raise_full(uint8_t* rec, uint8_t* ctx0) {
  if (run_veh(rec, ctx0, false)) { run_veh(rec, ctx0, true); return true; }
  if (dispatch(rec, ctx0)) { run_veh(rec, ctx0, true); return true; }
  uint64_t flt; { std::lock_guard<std::mutex> l(g_mu); flt = g_filter; }
  if (flt) {
    uint64_t ptrs[2] = {(uint64_t)(uintptr_t)rec, (uint64_t)(uintptr_t)ctx0};
    int32_t r = ((int32_t (MSABI *)(uint64_t*))(uintptr_t)flt)(ptrs);
    if (trace()) std::fprintf(stderr, "seh: unhandled filter returned %d\n", r);
    if (r == -1) return true;
  }
  std::function<void(uint32_t)> h; { std::lock_guard<std::mutex> l(g_mu); h = g_unhandled; }
  if (h) h(u32(rec));
  return false;
}
// Phase 2: unwind from start up to target_frame, running termination handlers, then continue at target_ip. Does not return.
[[noreturn]] void unwind_to(uint64_t target_frame, uint64_t target_ip, uint8_t* rec, uint64_t retval, const uint8_t* start) {
  uint8_t local_rec[0x98] = {};
  if (!rec) { rec = local_rec; uint32_t code = 0x80000027; std::memcpy(rec, &code, 4); }
  uint32_t flags = u32(rec + 4) | EH_UNWINDING | (target_frame ? 0 : EH_EXIT_UNWIND);
  uint8_t c[CTX_SIZE], c2[CTX_SIZE];
  std::memcpy(c, start, CTX_SIZE);
  cont_prune(*(const uint64_t*)(start + O_RSP));
  long idx = (long)t_cont.size() - 1;
  for (int depth = 0; depth < 512; depth++) {
    uint64_t pc = RIP(c), base = 0;
    const RF* rf = lookup(pc, &base);
    if (!rf) {
      if (trace()) std::fprintf(stderr, "seh:  unwind: no function entry for pc=%llx\n", (unsigned long long)pc);
      if (!in_image(pc)) {
        // A guest handler was called by our host dispatcher, which has no unwind tables: carry on from where the exception was raised,
        // as the real stack would (handler, dispatcher, RaiseException's caller).
        if (idx >= 0) { std::memcpy(c, t_cont[idx--].c, CTX_SIZE); continue; }
        break;
      }
      leaf_unwind(c); continue;
    }
    std::memcpy(c2, c, CTX_SIZE);
    uint64_t hdata = 0, frame = 0;
    uint64_t h = virtual_unwind(2, base, pc, rf, c2, &hdata, &frame);
    bool target = target_frame && frame == target_frame;
    if (trace()) std::fprintf(stderr, "seh:  unwind frame pc=%llx rva=%llx frame=%llx handler=%llx target=%d\n", (unsigned long long)pc, (unsigned long long)(pc - base), (unsigned long long)frame, (unsigned long long)(h ? h - base : 0), (int)target);
    if (h) {
      uint32_t f = flags | (target ? EH_TARGET_UNWIND : 0); std::memcpy(rec + 4, &f, 4);
      DC dc{pc, base, (uint64_t)(uintptr_t)rf, frame, target_ip, (uint64_t)(uintptr_t)c, h, hdata, 0, 0, 0};
      size_t depth_now = t_cont.size();
      cont_push(c2);
      ((Handler)(uintptr_t)h)(rec, frame, c, &dc);
      t_cont.resize(depth_now);
    }
    if (target) break;
    std::memcpy(c, c2, CTX_SIZE);
  }
  if (trace()) std::fprintf(stderr, "seh: continue at %llx rsp=%llx\n", (unsigned long long)target_ip, (unsigned long long)RSP(c));
  cont_prune(RSP(c));
  RIP(c) = target_ip; R(c, 0) = retval;
  if (__asan_handle_no_return) __asan_handle_no_return();
  anyxbox_seh_restore(c);
  __builtin_unreachable();
}
}  // namespace
}  // namespace anyxbox
extern "C" {
using namespace anyxbox;
MSABI uint64_t anyxbox_seh_impl_RaiseException(uint64_t code, uint64_t flags, uint64_t nargs, uint64_t args, uint64_t, uint64_t, uint8_t* ctx) {
  uint8_t rec[0x98] = {};
  uint32_t c = (uint32_t)code, f = (uint32_t)flags, n = nargs > 15 ? 15 : (uint32_t)nargs;
  std::memcpy(rec, &c, 4); std::memcpy(rec + 4, &f, 4);
  uint64_t addr = RIP(ctx); std::memcpy(rec + 16, &addr, 8); std::memcpy(rec + 24, &n, 4);
  if (args) for (uint32_t i = 0; i < n; i++) std::memcpy(rec + 32 + 8 * i, (const void*)(uintptr_t)(args + 8 * i), 8);
  raise_full(rec, ctx);
  return 0;
}
MSABI uint64_t anyxbox_seh_impl_RtlCaptureContext(uint64_t out, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint8_t* ctx) {
  if (out) std::memcpy((void*)(uintptr_t)out, ctx, CTX_SIZE);
  return 0;
}
MSABI uint64_t anyxbox_seh_impl_RtlUnwindEx(uint64_t target_frame, uint64_t target_ip, uint64_t rec, uint64_t retval, uint64_t, uint64_t, uint8_t* ctx) {
  if (trace()) std::fprintf(stderr, "seh: RtlUnwindEx target_frame=%llx target_ip=%llx\n", (unsigned long long)target_frame, (unsigned long long)target_ip);
  unwind_to(target_frame, target_ip, (uint8_t*)(uintptr_t)rec, retval, ctx);
}
MSABI uint64_t anyxbox_seh_impl_RtlUnwind(uint64_t target_frame, uint64_t target_ip, uint64_t rec, uint64_t retval, uint64_t, uint64_t, uint8_t* ctx) {
  unwind_to(target_frame, target_ip, (uint8_t*)(uintptr_t)rec, retval, ctx);
}
}
namespace anyxbox {
namespace {
MSABI uint64_t s_AddVectoredExceptionHandler(uint32_t first, uint64_t fn) {
  std::lock_guard<std::mutex> l(g_mu); uint64_t id = 0x7e000000u + g_veh_next++; Veh v{id, fn, false};
  if (first) g_veh.insert(g_veh.begin(), v); else g_veh.push_back(v);
  return id;
}
MSABI uint64_t s_AddVectoredContinueHandler(uint32_t first, uint64_t fn) {
  std::lock_guard<std::mutex> l(g_mu); uint64_t id = 0x7e000000u + g_veh_next++; Veh v{id, fn, true};
  if (first) g_veh.insert(g_veh.begin(), v); else g_veh.push_back(v);
  return id;
}
MSABI uint32_t s_RemoveVectoredExceptionHandler(uint64_t id) {
  std::lock_guard<std::mutex> l(g_mu);
  for (size_t i = 0; i < g_veh.size(); i++) if (g_veh[i].id == id) { g_veh.erase(g_veh.begin() + i); return 1; }
  return 0;
}
MSABI uint64_t s_SetUnhandledExceptionFilter(uint64_t fn) { std::lock_guard<std::mutex> l(g_mu); uint64_t old = g_filter; g_filter = fn; return old; }
MSABI int32_t s_UnhandledExceptionFilter(uint64_t* ptrs) {
  uint64_t flt; { std::lock_guard<std::mutex> l(g_mu); flt = g_filter; }
  if (flt) return ((int32_t (MSABI *)(uint64_t*))(uintptr_t)flt)(ptrs);
  return 1;   // EXCEPTION_EXECUTE_HANDLER: no filter, the process ends
}
MSABI void s_RtlRestoreContext(uint8_t* ctx, uint8_t*) { if (__asan_handle_no_return) __asan_handle_no_return(); anyxbox_seh_restore(ctx); }
MSABI const RF* s_RtlLookupFunctionEntry(uint64_t pc, uint64_t* base, void*) { uint64_t b = 0; const RF* r = lookup(pc, &b); if (base) *base = r ? b : 0; return r; }
MSABI void* s_RtlVirtualUnwind(uint32_t type, uint64_t base, uint64_t pc, const RF* rf, uint8_t* ctx, uint64_t* hdata, uint64_t* frame, void*) {
  uint64_t fr = 0, hd = 0;
  if (!rf) { leaf_unwind(ctx); if (frame) *frame = RSP(ctx); return nullptr; }
  uint64_t h = virtual_unwind(type, base, pc, rf, ctx, &hd, &fr);
  if (frame) *frame = fr;
  if (hdata && h) *hdata = hd;
  return (void*)(uintptr_t)h;
}
// C scope tables (MSVC and MinGW try/except/finally): count, then {begin, end, handler-or-filter, jump target} RVAs.
MSABI int32_t s_C_specific_handler(uint8_t* rec, uint64_t frame, uint8_t* ctx, DC* dc) {
  const uint8_t* tbl = (const uint8_t*)(uintptr_t)dc->HandlerData;
  uint32_t count = u32(tbl); uint32_t flags = u32(rec + 4);
  uint64_t base = dc->ImageBase; uint32_t pc = (uint32_t)(dc->ControlPc - base);
  if (flags & (EH_UNWINDING | EH_EXIT_UNWIND)) {
    for (uint32_t i = dc->ScopeIndex; i < count; i++) {
      const uint8_t* e = tbl + 4 + 16 * i;
      uint32_t b = u32(e), en = u32(e + 4), h = u32(e + 8), tgt = u32(e + 12);
      if (pc < b || pc >= en) continue;
      if ((flags & EH_TARGET_UNWIND) && dc->TargetIp >= base + b && dc->TargetIp < base + en) break;
      if (tgt == 0) { dc->ScopeIndex = i + 1; ((void (MSABI *)(int32_t, uint64_t))(uintptr_t)(base + h))(1, frame); }
    }
    return 1;   // ExceptionContinueSearch
  }
  for (uint32_t i = dc->ScopeIndex; i < count; i++) {
    const uint8_t* e = tbl + 4 + 16 * i;
    uint32_t b = u32(e), en = u32(e + 4), h = u32(e + 8), tgt = u32(e + 12);
    if (pc < b || pc >= en || tgt == 0) continue;
    int32_t r = 1;
    if (h != 1) {
      uint64_t ptrs[2] = {(uint64_t)(uintptr_t)rec, (uint64_t)(uintptr_t)ctx};
      r = ((int32_t (MSABI *)(void*, uint64_t))(uintptr_t)(base + h))(ptrs, frame);
    }
    if (r < 0) return 0;                                      // EXCEPTION_CONTINUE_EXECUTION
    if (r > 0) unwind_to(frame, base + tgt, rec, u32(rec), t_ctx0 ? t_ctx0 : ctx);   // EXCEPTION_EXECUTE_HANDLER
  }
  return 1;
}
}  // namespace

// ---- hardware faults inside guest code become guest exceptions ----
// Save the fault in thread-local storage, then let sigreturn resume in fault_dispatch. Do not write below the faulting RSP
// while the signal handler is active: that area contains the Linux signal frame and the handler stack.
namespace {
struct sigaction g_old[4];
const int kSigs[4] = {SIGSEGV, SIGFPE, SIGILL, SIGBUS};
struct FaultPending { alignas(16) uint8_t ctx[CTX_SIZE]; uint8_t rec[0xA0]; };
thread_local FaultPending t_fault;
[[noreturn]] void fault_dispatch() {
  // Copy before invoking callbacks, which may themselves fault and reuse t_fault.
  alignas(16) uint8_t ctx[CTX_SIZE], rec[0xA0];
  std::memcpy(ctx, t_fault.ctx, sizeof ctx);
  std::memcpy(rec, t_fault.rec, sizeof rec);
  if (!raise_full(rec, ctx)) _exit(1);
  if (__asan_handle_no_return) __asan_handle_no_return();
  anyxbox_seh_restore(ctx);   // handler chose EXCEPTION_CONTINUE_EXECUTION: resume at the faulting instruction
  __builtin_unreachable();
}
void chain(int sig, siginfo_t* si, void* uc) {
  for (int i = 0; i < 4; i++) if (kSigs[i] == sig) {
    const struct sigaction& o = g_old[i];
    if (o.sa_flags & SA_SIGINFO) { if (o.sa_sigaction) { o.sa_sigaction(sig, si, uc); return; } }
    else if (o.sa_handler != SIG_DFL && o.sa_handler != SIG_IGN) { o.sa_handler(sig); return; }
    break;
  }
  signal(sig, SIG_DFL);   // nobody else handles it: the instruction faults again and the default action runs
}
void on_fault(int sig, siginfo_t* si, void* ucv) {
  ucontext_t* uc = (ucontext_t*)ucv;
  greg_t* g = uc->uc_mcontext.gregs;
  uint64_t rip = (uint64_t)g[REG_RIP];
  bool guest = false, active = false;
  if (g_mu.try_lock()) {
    active = (bool)g_unhandled;
    for (auto& im : g_imgs) if (rip >= im.base && rip < im.base + im.size) guest = true;
    g_mu.unlock();
  }
  if (!guest || !active) { chain(sig, si, ucv); return; }
  uint64_t rsp = (uint64_t)g[REG_RSP];
  uint8_t* ctx = t_fault.ctx; uint8_t* rec = t_fault.rec;
  std::memset(ctx, 0, CTX_SIZE); std::memset(rec, 0, 0xA0);
  const int map[16] = {REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP, REG_RSI, REG_RDI, REG_R8, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15};
  for (int i = 0; i < 16; i++) R(ctx, i) = (uint64_t)g[map[i]];
  RIP(ctx) = rip;
  uint32_t flags = 0x10001F, eflags = (uint32_t)g[REG_EFL];
  std::memcpy(ctx + O_FLAGS, &flags, 4); std::memcpy(ctx + 0x44, &eflags, 4);
  if (uc->uc_mcontext.fpregs) {
    for (int i = 0; i < 16; i++) std::memcpy(ctx + O_XMM + 16 * i, &uc->uc_mcontext.fpregs->_xmm[i], 16);
    std::memcpy(ctx + 0x34, &uc->uc_mcontext.fpregs->mxcsr, 4);
  }
  uint32_t code = 0xC0000005, nparams = 0; uint64_t p0 = 0, p1 = 0;
  if (sig == SIGSEGV || sig == SIGBUS) { code = 0xC0000005; nparams = 2; p0 = (g[REG_ERR] & 2) ? 1 : 0; p1 = (uint64_t)(uintptr_t)si->si_addr; }
  else if (sig == SIGFPE) { code = si->si_code == FPE_INTDIV ? 0xC0000094u : si->si_code == FPE_INTOVF ? 0xC0000095u : si->si_code == FPE_FLTDIV ? 0xC000008Eu : 0xC0000090u; }
  else if (sig == SIGILL) code = 0xC000001D;
  std::memcpy(rec, &code, 4); std::memcpy(rec + 16, &rip, 8); std::memcpy(rec + 24, &nparams, 4); std::memcpy(rec + 32, &p0, 8); std::memcpy(rec + 40, &p1, 8);
  // Enter with SysV call alignment and leave the interrupted red zone untouched. No return-address write here: sigreturn still
  // needs the current signal frame intact. fault_dispatch is noreturn and starts using this stack only after sigreturn completes.
  g[REG_RSP] = (greg_t)(((rsp - 128) & ~0xFull) - 8);
  g[REG_RIP] = (greg_t)(uintptr_t)&fault_dispatch;
}
}  // namespace
void seh_install_fault_handlers() {
  static std::once_flag once;
  std::call_once(once, [] {
    for (int i = 0; i < 4; i++) {
      struct sigaction sa; std::memset(&sa, 0, sizeof sa);
      sa.sa_sigaction = on_fault; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
      sigaction(kSigs[i], &sa, &g_old[i]);
    }
  });
}
void seh_register_image(uint64_t base, uint32_t size, uint32_t pdata_rva, uint32_t pdata_size) {
  std::lock_guard<std::mutex> l(g_mu);
  g_imgs.push_back({base, size, pdata_rva, pdata_size / 12});
}
void seh_unregister_image(uint64_t base) {
  std::lock_guard<std::mutex> l(g_mu);
  for (size_t i = 0; i < g_imgs.size(); i++) if (g_imgs[i].base == base) { g_imgs.erase(g_imgs.begin() + (long)i); break; }
}
void seh_set_unhandled(std::function<void(uint32_t)> fn) { std::lock_guard<std::mutex> l(g_mu); g_unhandled = std::move(fn); if (!g_unhandled) { g_veh.clear(); g_filter = 0; } }
bool seh_register(ImportRegistry& r) {
  auto A = [](auto* f) { return (uint64_t)(uintptr_t)f; };
  for (const char* dll : {"kernel32.dll", "ntdll.dll"}) {
    r.add(dll, "RaiseException", A(&anyxbox_seh_RaiseException)); r.add(dll, "RtlCaptureContext", A(&anyxbox_seh_RtlCaptureContext));
    r.add(dll, "RtlRestoreContext", A(&s_RtlRestoreContext)); r.add(dll, "RtlLookupFunctionEntry", A(&s_RtlLookupFunctionEntry));
    r.add(dll, "AddVectoredExceptionHandler", A(&s_AddVectoredExceptionHandler)); r.add(dll, "AddVectoredContinueHandler", A(&s_AddVectoredContinueHandler));
    r.add(dll, "RemoveVectoredExceptionHandler", A(&s_RemoveVectoredExceptionHandler)); r.add(dll, "RemoveVectoredContinueHandler", A(&s_RemoveVectoredExceptionHandler));
    r.add(dll, "SetUnhandledExceptionFilter", A(&s_SetUnhandledExceptionFilter)); r.add(dll, "UnhandledExceptionFilter", A(&s_UnhandledExceptionFilter));
    r.add(dll, "RtlVirtualUnwind", A(&s_RtlVirtualUnwind)); r.add(dll, "RtlUnwind", A(&anyxbox_seh_RtlUnwind)); r.add(dll, "RtlUnwindEx", A(&anyxbox_seh_RtlUnwindEx));
  }
  for (const char* dll : {"msvcrt.dll", "ucrtbase.dll", "vcruntime140.dll"}) r.add(dll, "__C_specific_handler", A(&s_C_specific_handler));
  return true;
}
}
#else
namespace anyxbox {
void seh_register_image(uint64_t, uint32_t, uint32_t, uint32_t) {}
void seh_unregister_image(uint64_t) {}
void seh_set_unhandled(std::function<void(uint32_t)>) {}
void seh_install_fault_handlers() {}
bool seh_register(ImportRegistry&) { return false; }
}
#endif
