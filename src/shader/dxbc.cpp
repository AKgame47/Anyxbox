#include "shader/dxbc.h"
#include <cstring>
namespace anyxbox { namespace shader {
namespace {
uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
}

DxbcContainer parse_dxbc(const uint8_t* d, size_t n) {
  DxbcContainer c;
  auto fail = [&](const char* m) { c.ok = false; c.error = m; return c; };
  if (n < 32 || std::memcmp(d, "DXBC", 4) != 0) return fail("not a DXBC container");
  if (rd32(d + 20) != 1) return fail("unsupported DXBC version");
  uint32_t total = rd32(d + 24), count = rd32(d + 28);
  if (total != n) return fail("container size field does not match the data size");
  if (count == 0 || count > 64 || 32 + (uint64_t)count * 4 > n) return fail("bad chunk count");
  for (uint32_t i = 0; i < count; i++) {
    uint32_t off = rd32(d + 32 + i * 4);
    if (off < 32 + count * 4 || (uint64_t)off + 8 > n) return fail("chunk header out of range");
    uint32_t sz = rd32(d + off + 4);
    if ((uint64_t)off + 8 + sz > n) return fail("chunk data out of range");
    std::string name((const char*)d + off, 4);
    c.chunk_names.push_back(name);
    const uint8_t* body = d + off + 8;
    if (name == "ISGN" || name == "OSGN") {
      if (sz < 8) return fail("signature chunk too small");
      uint32_t ec = rd32(body);
      if (ec > 64 || 8 + (uint64_t)ec * 24 > sz) return fail("signature element table out of range");
      auto& out = name == "ISGN" ? c.inputs : c.outputs;
      for (uint32_t e = 0; e < ec; e++) {
        const uint8_t* p = body + 8 + e * 24;
        SigElement s; uint32_t no = rd32(p);
        s.index = rd32(p + 4); s.sysval = rd32(p + 8); s.comp_type = rd32(p + 12); s.reg = rd32(p + 16);
        s.mask = p[20]; s.rw_mask = p[21];
        if (no >= sz) return fail("signature name offset out of range");
        size_t len = 0; while (no + len < sz && body[no + len]) len++;
        if (no + len >= sz) return fail("signature name not terminated");
        s.name.assign((const char*)body + no, len);
        out.push_back(s);
      }
    } else if (name == "SHDR" || name == "SHEX") {
      if (c.shader) return fail("more than one shader chunk");
      c.shader = body; c.shader_size = sz;
    }
  }
  if (!c.shader) return fail("no SHDR/SHEX chunk");
  c.ok = true; return c;
}

namespace {
struct Cursor { const uint32_t* w; size_t n, pos = 0; };

bool parse_operand(const uint32_t* w, size_t avail, Operand& o, size_t& used, std::string& err) {
  if (avail < 1) { err = "operand runs past the instruction"; return false; }
  uint32_t t = w[0]; size_t p = 1;
  o.ncomp = t & 3; o.selmode = (t >> 2) & 3; o.type = (t >> 12) & 0xFF; o.index_dim = (t >> 20) & 3;
  uint32_t bits = (t >> 4) & 0xFF;
  if (o.ncomp == 2) {
    if (o.selmode == SEL_MASK) o.mask = bits & 0xF;
    else if (o.selmode == SEL_SWIZZLE) for (int i = 0; i < 4; i++) o.swizzle[i] = (bits >> (2 * i)) & 3;
    else if (o.selmode == SEL_SELECT1) for (int i = 0; i < 4; i++) o.swizzle[i] = bits & 3;
    else { err = "unknown component selection mode"; return false; }
  } else if (o.ncomp == 3) { err = "N-component operands are not supported"; return false; }
  bool ext = (t >> 31) != 0;
  while (ext) {
    if (p >= avail) { err = "operand extension runs past the instruction"; return false; }
    uint32_t e = w[p++];
    if ((e & 0x3F) == 1) { uint32_t m = (e >> 6) & 0xFF; o.neg = (m == 1 || m == 3); o.abs = (m == 2 || m == 3); }
    ext = (e >> 31) != 0;
  }
  if (o.type == OT_IMM32) {
    size_t cnt = o.ncomp == 1 ? 1 : o.ncomp == 2 ? 4 : 0;
    if (!cnt || p + cnt > avail) { err = "bad immediate operand"; return false; }
    for (size_t i = 0; i < cnt; i++) o.imm[i] = w[p++];
    if (cnt == 1) for (int i = 1; i < 4; i++) o.imm[i] = o.imm[0];
  } else {
    for (uint32_t i = 0; i < o.index_dim; i++) {
      uint32_t rep = (t >> (22 + 3 * i)) & 7;
      if (rep != 0) { err = "relative or 64-bit operand indices are not supported"; return false; }
      if (p >= avail) { err = "operand index runs past the instruction"; return false; }
      o.index[i] = w[p++];
    }
  }
  used = p; return true;
}

int operand_count(uint32_t op) {
  switch (op) {
    case OP_MOV: case OP_FRC: case OP_RSQ: case OP_SQRT: case OP_EXP: case OP_LOG: return 2;
    case OP_ADD: case OP_MUL: case OP_DIV: case OP_MIN: case OP_MAX: case OP_DP2: case OP_DP3: case OP_DP4: return 3;
    case OP_MAD: case OP_MOVC: return 4;
    case OP_IF: case OP_BREAKC: case OP_CONTINUEC: case OP_RETC: case OP_DISCARD: return 1;
    case OP_ELSE: case OP_ENDIF: case OP_LOOP: case OP_ENDLOOP: case OP_BREAK: case OP_CONTINUE: return 0;
    case OP_ITOF: case OP_FTOI: case OP_INEG: case OP_NOT: return 2;
    case OP_EQ: case OP_NE: case OP_LT: case OP_GE: case OP_IEQ: case OP_INE: case OP_ILT: case OP_IGE:
    case OP_IADD: case OP_AND: case OP_OR: case OP_XOR: return 3;
    case OP_SAMPLE: return 4;
    case OP_SAMPLE_L: return 5;
    case OP_DCL_RESOURCE: case OP_DCL_SAMPLER: return 1;
    case OP_RET: return 0;
    case OP_DCL_INPUT: case OP_DCL_INPUT_PS: case OP_DCL_OUTPUT: case OP_DCL_CONSTANT_BUFFER:
    case OP_DCL_INPUT_SIV: case OP_DCL_INPUT_SGV: case OP_DCL_INPUT_PS_SIV: case OP_DCL_INPUT_PS_SGV:
    case OP_DCL_OUTPUT_SIV: case OP_DCL_OUTPUT_SGV: return 1;
    default: return -1;
  }
}
}  // namespace

Program decode_shader(const uint8_t* d, size_t n) {
  Program pr;
  auto fail = [&](const std::string& m) { pr.ok = false; pr.error = m; return pr; };
  if (n < 8 || n % 4) return fail("shader chunk too small or not dword aligned");
  size_t nw = n / 4; std::vector<uint32_t> w(nw);
  std::memcpy(w.data(), d, n);
  pr.type = w[0] >> 16; pr.major = (w[0] >> 4) & 0xF; pr.minor = w[0] & 0xF;
  if (w[1] != nw) return fail("shader length token does not match the chunk size");
  size_t i = 2;
  while (i < nw) {
    uint32_t t = w[i];
    uint32_t op = t & 0x7FF, len = (t >> 24) & 0x7F;
    if (op == OP_CUSTOMDATA) { if (i + 1 >= nw || w[i + 1] < 2 || i + w[i + 1] > nw) return fail("bad custom data block"); i += w[i + 1]; continue; }
    if (len == 0 || i + len > nw) return fail("instruction length runs past the shader");
    Instruction in; in.opcode = op; in.saturate = (t & 0x2000) != 0; in.control = (t >> 11) & 0x1FFF;
    size_t p = i + 1; bool ext = (t >> 31) != 0;
    while (ext) { if (p >= i + len) return fail("opcode extension runs past the instruction"); ext = (w[p++] >> 31) != 0; }
    int oc = operand_count(op);
    if (oc < 0) {
      if (op == OP_DCL_TEMPS) { if (len != 2) return fail("bad dcl_temps"); in.value = w[i + 1]; }
      else if (op == OP_DCL_GLOBAL_FLAGS) { /* control bits only */ }
      else return fail("unsupported opcode " + std::to_string(op));
    } else {
      for (int k = 0; k < oc; k++) {
        Operand o; size_t used = 0; std::string err;
        if (!parse_operand(&w[p], i + len - p, o, used, err)) return fail(err + " (opcode " + std::to_string(op) + ")");
        p += used; in.ops.push_back(o);
      }
      bool siv = op == OP_DCL_INPUT_SIV || op == OP_DCL_INPUT_SGV || op == OP_DCL_INPUT_PS_SIV || op == OP_DCL_INPUT_PS_SGV ||
                 op == OP_DCL_OUTPUT_SIV || op == OP_DCL_OUTPUT_SGV;
      if (op == OP_DCL_RESOURCE) { if (p >= i + len) return fail("missing resource return type token"); in.value = w[p++]; }
      if (siv) { if (p >= i + len) return fail("missing system value token"); in.value = w[p++]; }
      if (p != i + len) return fail("instruction length does not match its operands (opcode " + std::to_string(op) + ")");
    }
    pr.instructions.push_back(std::move(in));
    i += len;
  }
  pr.ok = true; return pr;
}
}}
