#include "shader/dxbc_spirv.h"
#include <algorithm>
#include <cstring>
#include <map>
namespace anyxbox { namespace shader {
namespace {
enum { sOpExtInst = 12, sOpExtInstImport = 11, sOpMemoryModel = 14, sOpEntryPoint = 15, sOpExecutionMode = 16, sOpCapability = 17,
       sOpTypeBool = 20, sOpBitcast = 124, sOpSNegate = 126, sOpIAdd = 128, sOpConvertFToS = 110, sOpConvertSToF = 111,
       sOpSelect = 169, sOpIEqual = 170, sOpINotEqual = 171, sOpSGreaterThanEqual = 175, sOpSLessThan = 177,
       sOpFOrdEqual = 180, sOpFUnordNotEqual = 183, sOpFOrdLessThan = 184, sOpFOrdGreaterThanEqual = 190,
       sOpBitwiseOr = 197, sOpBitwiseXor = 198, sOpBitwiseAnd = 199, sOpNot = 200,
       sOpLoopMerge = 246, sOpSelectionMerge = 247, sOpBranch = 249, sOpBranchConditional = 250, sOpKill = 252,
       sOpTypeVoid = 19, sOpTypeInt = 21, sOpTypeImage = 25, sOpTypeSampledImage = 27, sOpImageSampleImplicitLod = 87, sOpImageSampleExplicitLod = 88, sOpTypeFloat = 22, sOpTypeArray = 28, sOpTypeStruct = 30, sOpAccessChain = 65, sOpMemberDecorate = 72, sOpTypeVector = 23, sOpTypePointer = 32, sOpTypeFunction = 33,
       sOpConstant = 43, sOpConstantComposite = 44, sOpFunction = 54, sOpFunctionEnd = 56, sOpVariable = 59,
       sOpLoad = 61, sOpStore = 62, sOpDecorate = 71, sOpVectorShuffle = 79, sOpCompositeConstruct = 80,
       sOpCompositeExtract = 81, sOpFNegate = 127, sOpFAdd = 129, sOpFMul = 133, sOpFDiv = 136, sOpDot = 148,
       sOpLabel = 248, sOpReturn = 253 };
enum { GL_FAbs = 4, GL_Fract = 10, GL_Exp2 = 29, GL_Log2 = 30, GL_Sqrt = 31, GL_InvSqrt = 32, GL_FMin = 37, GL_FMax = 40, GL_FClamp = 43 };
enum { SC_Input = 1, SC_Output = 3, SC_Function = 7 };

struct B {
  uint32_t next = 1;
  std::vector<uint32_t> caps, ext, mem, entry, modes, decor, globals, body;
  uint32_t id() { return next++; }
  static void put(std::vector<uint32_t>& v, uint32_t op, std::initializer_list<uint32_t> a) {
    v.push_back(((uint32_t)(a.size() + 1) << 16) | op); v.insert(v.end(), a.begin(), a.end());
  }
  static void putv(std::vector<uint32_t>& v, uint32_t op, const std::vector<uint32_t>& a) {
    v.push_back(((uint32_t)(a.size() + 1) << 16) | op); v.insert(v.end(), a.begin(), a.end());
  }
  uint32_t t_void = 0, t_fn = 0, t_float = 0, glsl = 0;
  std::map<int, uint32_t> vec;                    // width -> type id
  std::map<std::pair<int, int>, uint32_t> ptr;    // (storage, width) -> pointer type
  std::map<uint32_t, uint32_t> fconst;            // bits -> id
  std::map<std::vector<uint32_t>, uint32_t> cconst;
  uint32_t T(int w) {
    if (w == 1) return t_float;
    auto it = vec.find(w); if (it != vec.end()) return it->second;
    uint32_t r = id(); put(globals, sOpTypeVector, {r, t_float, (uint32_t)w}); return vec[w] = r;
  }
  uint32_t P(int sc, int w) {
    auto k = std::make_pair(sc, w); auto it = ptr.find(k); if (it != ptr.end()) return it->second;
    uint32_t r = id(); put(globals, sOpTypePointer, {r, (uint32_t)sc, T(w)}); return ptr[k] = r;
  }
  uint32_t F(uint32_t bits) {
    auto it = fconst.find(bits); if (it != fconst.end()) return it->second;
    uint32_t r = id(); put(globals, sOpConstant, {t_float, r, bits}); return fconst[bits] = r;
  }
  uint32_t C4(const uint32_t b[4]) {
    std::vector<uint32_t> k(b, b + 4); auto it = cconst.find(k); if (it != cconst.end()) return it->second;
    uint32_t r = id(); put(globals, sOpConstantComposite, {T(4), r, F(b[0]), F(b[1]), F(b[2]), F(b[3])}); return cconst[k] = r;
  }
  uint32_t var(int sc, int w) { uint32_t r = id(); put(globals, sOpVariable, {P(sc, w), r, (uint32_t)sc}); return r; }
  // body helpers
  uint32_t load(int w, uint32_t p) { uint32_t r = id(); put(body, sOpLoad, {T(w), r, p}); return r; }
  void store(uint32_t p, uint32_t v) { put(body, sOpStore, {p, v}); }
  uint32_t shuffle(int outw, uint32_t a, uint32_t b, const std::vector<uint32_t>& idx) {
    uint32_t r = id(); std::vector<uint32_t> o = {T(outw), r, a, b}; o.insert(o.end(), idx.begin(), idx.end());
    putv(body, sOpVectorShuffle, o); return r;
  }
  uint32_t bin(uint32_t op, int w, uint32_t a, uint32_t c) { uint32_t r = id(); put(body, op, {T(w), r, a, c}); return r; }
  uint32_t un(uint32_t op, int w, uint32_t a) { uint32_t r = id(); put(body, op, {T(w), r, a}); return r; }
  uint32_t gl(int w, uint32_t fn, std::initializer_list<uint32_t> args) {
    uint32_t r = id(); std::vector<uint32_t> o = {T(w), r, glsl, fn}; o.insert(o.end(), args.begin(), args.end());
    putv(body, sOpExtInst, o); return r;
  }
  uint32_t t_bool = 0, t_bvec4 = 0, t_uint1 = 0, t_uvec4 = 0, t_int1 = 0, t_ivec4 = 0;
  std::map<uint32_t, uint32_t> uconst;
  uint32_t BOOL1() { if (!t_bool) { t_bool = id(); put(globals, sOpTypeBool, {t_bool}); } return t_bool; }
  uint32_t BOOL4() { if (!t_bvec4) { uint32_t e = BOOL1(); t_bvec4 = id(); put(globals, sOpTypeVector, {t_bvec4, e, 4}); } return t_bvec4; }
  uint32_t U1() { if (!t_uint1) { t_uint1 = id(); put(globals, sOpTypeInt, {t_uint1, 32, 0}); } return t_uint1; }
  uint32_t U4() { if (!t_uvec4) { uint32_t e = U1(); t_uvec4 = id(); put(globals, sOpTypeVector, {t_uvec4, e, 4}); } return t_uvec4; }
  uint32_t I1() { if (!t_int1) { t_int1 = id(); put(globals, sOpTypeInt, {t_int1, 32, 1}); } return t_int1; }
  uint32_t I4() { if (!t_ivec4) { uint32_t e = I1(); t_ivec4 = id(); put(globals, sOpTypeVector, {t_ivec4, e, 4}); } return t_ivec4; }
  uint32_t UC(uint32_t bits) { auto it = uconst.find(bits); if (it != uconst.end()) return it->second; uint32_t t = U1(); uint32_t r = id(); put(globals, sOpConstant, {t, r, bits}); return uconst[bits] = r; }
  uint32_t UC4(uint32_t bits) { uint32_t c = UC(bits), t = U4(); uint32_t r = id(); put(globals, sOpConstantComposite, {t, r, c, c, c, c}); return r; }
  uint32_t cast(uint32_t type, uint32_t v) { uint32_t r = id(); put(body, sOpBitcast, {type, r, v}); return r; }
  uint32_t uzero = 0;
  uint32_t id_zero_uint(uint32_t) { return UC(0); }
  uint32_t extract(uint32_t v, uint32_t i) { uint32_t r = id(); put(body, sOpCompositeExtract, {t_float, r, v, i}); return r; }
  uint32_t construct4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { uint32_t r = id(); put(body, sOpCompositeConstruct, {T(4), r, a, b, c, d}); return r; }
};

struct Reg { uint32_t var = 0; int width = 4; bool builtin = false; };
uint32_t fbits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
}  // namespace

SpirvResult dxbc_to_spirv(const uint8_t* data, size_t size, bool vulkan_flip_y) {
  SpirvResult R;
  auto fail = [&](const std::string& m) { R.ok = false; R.error = m; return R; };
  DxbcContainer c = parse_dxbc(data, size);
  if (!c.ok) return fail("dxbc: " + c.error);
  Program p = decode_shader(c.shader, c.shader_size);
  if (!p.ok) return fail("sm4: " + p.error);
  if (p.type != 0 && p.type != 1) return fail("only vertex and pixel shaders are supported");
  if (p.major != 4 && p.major != 5) return fail("only shader model 4.x and 5.x are supported");
  const bool pixel = p.type == 0; R.is_pixel = pixel;

  B b;
  b.t_void = b.id(); b.put(b.globals, sOpTypeVoid, {b.t_void});
  b.t_fn = b.id(); b.put(b.globals, sOpTypeFunction, {b.t_fn, b.t_void});
  b.t_float = b.id(); b.put(b.globals, sOpTypeFloat, {b.t_float, 32});
  b.glsl = b.id();
  b.put(b.caps, sOpCapability, {1});
  { std::vector<uint32_t> e = {b.glsl}; const char* nm = "GLSL.std.450"; uint32_t w = 0; int k = 0;
    for (const char* q = nm;; q++) { w |= (uint32_t)(uint8_t)*q << (8 * k); if (++k == 4 || !*q) { e.push_back(w); w = 0; k = 0; if (!*q) break; } }
    if (k == 0 && e.back() != 0 && std::strlen(nm) % 4 == 0) e.push_back(0);
    b.putv(b.ext, sOpExtInstImport, e); }
  b.put(b.mem, sOpMemoryModel, {0, 1});

  std::map<uint32_t, Reg> in, out, tmp;
  std::vector<uint32_t> iface;
  bool have_smp = false; std::map<uint32_t, bool> tex_decl, smp_decl;   // declared texture slots
  uint32_t ntemps = 0, cb_count = 0, cb_var = 0, t_uint = 0;
  std::map<uint32_t, uint32_t> cb_regs, cb_off;   // slot -> declared registers / offset in the packed array (registers)
  // signature lookup helpers
  auto sig_for = [&](const std::vector<SigElement>& s, uint32_t reg) -> const SigElement* {
    for (auto& e : s) { if (e.reg == reg) return &e; }
    return nullptr; };
  // first pass: declarations
  std::vector<uint32_t> user_in_regs, user_out_regs;
  for (auto& I : p.instructions) {
    const Operand* o = I.ops.empty() ? nullptr : &I.ops[0];
    switch (I.opcode) {
      case OP_DCL_TEMPS: ntemps = I.value; if (ntemps > 4096) return fail("too many temporaries"); break;
      case OP_DCL_INPUT: case OP_DCL_INPUT_PS: {
        if (o->type != OT_INPUT || o->index_dim != 1) return fail("unsupported input declaration");
        Reg r; uint32_t reg = o->index[0];
        const SigElement* se = sig_for(c.inputs, reg);
        bool sv = se && se->sysval != 0;
        if (sv) return fail("unsupported system-value input (use dcl_input_ps_siv/sgv forms are not handled)");
        int w = 4; if (se && !pixel) { w = 1; for (int k = 0; k < 4; k++) if (se->mask & (1 << k)) w = k + 1; }
        r.width = w; in[reg] = r; user_in_regs.push_back(reg); break; }
      case OP_DCL_INPUT_PS_SIV: case OP_DCL_INPUT_SIV: {
        // SV_POSITION in a pixel shader is system value 1
        if (!pixel || I.value != 1 || o->index_dim != 1) return fail("unsupported system-value input");
        Reg r; r.builtin = true; r.width = 4; in[o->index[0]] = r; break; }
      case OP_DCL_OUTPUT: {
        if (o->type != OT_OUTPUT || o->index_dim != 1) return fail("unsupported output declaration");
        Reg r; r.width = 4; out[o->index[0]] = r; user_out_regs.push_back(o->index[0]); break; }
      case OP_DCL_OUTPUT_SIV: {
        if (pixel || I.value != 1 || o->index_dim != 1) return fail("unsupported system-value output (only vertex SV_POSITION)");
        Reg r; r.builtin = true; r.width = 4; out[o->index[0]] = r; break; }
      case OP_DCL_RESOURCE: {
        if (!pixel) return fail("texture sampling in vertex shaders is not supported yet");
        if (o->type != OT_RESOURCE || o->index_dim != 1) return fail("unsupported resource declaration");
        if ((I.control & 0x1F) != 3) return fail("only Texture2D resources are supported");
        if (o->index[0] > 3) return fail("only texture slots t0..t3 are supported");
        if (I.value != 0x5555) return fail("only float4 texture return types are supported");
        if (tex_decl.count(o->index[0])) return fail("texture declared twice");
        tex_decl[o->index[0]] = true; break; }
      case OP_DCL_SAMPLER: {
        if (!pixel) return fail("samplers in vertex shaders are not supported yet");
        if (o->type != OT_SAMPLER || o->index_dim != 1) return fail("unsupported sampler declaration");
        if (o->index[0] > 3) return fail("only sampler slots s0..s3 are supported");
        if (I.control & 0xF) return fail("only default-mode samplers are supported (no comparison samplers)");
        have_smp = true; smp_decl[o->index[0]] = true; break; }
      case OP_DCL_GLOBAL_FLAGS: break;
      case OP_DCL_CONSTANT_BUFFER: {
        if (o->type != OT_CBUFFER || o->index_dim != 2) return fail("unsupported constant buffer declaration");
        if (o->index[0] >= 14) return fail("constant buffer slot out of range (0..13)");
        if (cb_regs.count(o->index[0])) return fail("constant buffer slot declared twice");
        if (o->index[1] == 0 || o->index[1] > 1024) return fail("constant buffer must have 1 to 1024 registers (a size of 0, meaning unbounded, is not supported)");
        cb_regs[o->index[0]] = o->index[1]; break; }
      default: break;
    }
  }
  for (auto& kv : cb_regs) { cb_off[kv.first] = cb_count; cb_count += kv.second; if (cb_count > 1024) return fail("constant buffers together exceed 1024 registers"); R.cb_layout.push_back({kv.first, kv.second}); }
  { const bool simple = cb_regs.size() == 1 && cb_regs.count(0) && cb_count <= 8;
    bool dyn = false; for (auto& I : p.instructions) for (auto& op : I.ops) if (op.has_rel) dyn = true;
    R.vs_ubo = !pixel && cb_count && !(simple && !dyn); }
  const bool cb_ubo = pixel || R.vs_ubo;
  std::sort(user_in_regs.begin(), user_in_regs.end()); std::sort(user_out_regs.begin(), user_out_regs.end());
  // create variables
  for (auto& kv : in) {
    Reg& r = kv.second; r.var = b.var(SC_Input, r.width); iface.push_back(r.var);
    if (r.builtin) b.put(b.decor, sOpDecorate, {r.var, 11, 15});                // BuiltIn FragCoord
    else {
      uint32_t loc = pixel ? (uint32_t)(std::find(user_in_regs.begin(), user_in_regs.end(), kv.first) - user_in_regs.begin()) : kv.first;
      b.put(b.decor, sOpDecorate, {r.var, 30, loc});
    }
  }
  for (auto& kv : out) {
    Reg& r = kv.second; r.var = b.var(SC_Output, 4); iface.push_back(r.var);
    if (r.builtin) b.put(b.decor, sOpDecorate, {r.var, 11, 0});                 // BuiltIn Position
    else {
      uint32_t loc = pixel ? kv.first : (uint32_t)(std::find(user_out_regs.begin(), user_out_regs.end(), kv.first) - user_out_regs.begin());
      b.put(b.decor, sOpDecorate, {r.var, 30, loc});
    }
  }
  if (pixel && out.empty()) return fail("pixel shader declares no output");
  if (!pixel && out.empty()) return fail("vertex shader declares no output");
  if (cb_count) {
    t_uint = b.U1();
    uint32_t len = b.id(); b.put(b.globals, sOpConstant, {t_uint, len, cb_count});
    uint32_t arr = b.id(); b.put(b.globals, sOpTypeArray, {arr, b.T(4), len});
    uint32_t st = b.id(); b.put(b.globals, sOpTypeStruct, {st, arr});
    const uint32_t csc = cb_ubo ? 2 : 9;   // Uniform (set 1, binding 0 for pixel, 1 for vertex) or PushConstant (small static vertex cb0)
    uint32_t pp = b.id(); b.put(b.globals, sOpTypePointer, {pp, csc, st});
    cb_var = b.id(); b.put(b.globals, sOpVariable, {pp, cb_var, csc});
    if (cb_ubo) { b.put(b.decor, sOpDecorate, {cb_var, 34, 1}); b.put(b.decor, sOpDecorate, {cb_var, 33, pixel ? 0u : 1u}); }
    b.put(b.decor, sOpDecorate, {arr, 6, 16});
    b.put(b.decor, sOpDecorate, {st, 2});
    b.put(b.decor, sOpMemberDecorate, {st, 0, 35, 0});
  }
  uint32_t tex_var[4] = {0, 0, 0, 0}, t_simg = 0;
  const bool have_tex = !tex_decl.empty();
  if (have_tex != have_smp) return fail("textures and samplers must be declared together");
  if (have_tex) {
    uint32_t img = b.id(); b.put(b.globals, sOpTypeImage, {img, b.t_float, 1, 0, 0, 0, 1, 0});
    t_simg = b.id(); b.put(b.globals, sOpTypeSampledImage, {t_simg, img});
    uint32_t pt = b.id(); b.put(b.globals, sOpTypePointer, {pt, 0, t_simg});
    for (auto& kv : tex_decl) {
      tex_var[kv.first] = b.id(); b.put(b.globals, sOpVariable, {pt, tex_var[kv.first], 0});
      b.put(b.decor, sOpDecorate, {tex_var[kv.first], 34, 0}); b.put(b.decor, sOpDecorate, {tex_var[kv.first], 33, kv.first});   // set 0, binding N
    }
  }
  // temps live in the function (declared at the start of the entry block below)
  uint32_t main_id = b.id(), label = b.id();
  for (uint32_t i = 0; i < ntemps; i++) { Reg r; r.var = b.id(); tmp[i] = r; }
  std::vector<uint32_t> tmp_decl;
  for (uint32_t i = 0; i < ntemps; i++) b.put(tmp_decl, sOpVariable, {b.P(SC_Function, 4), tmp[i].var, (uint32_t)SC_Function});

  const uint32_t one = b.F(fbits(1.0f)), zero = b.F(fbits(0.0f));
  // ---- instruction translation ----
  std::string err;
  auto load_src = [&](const Operand& o, uint32_t& v) -> bool {
    uint32_t base = 0;
    if (o.type == OT_IMM32) { base = b.C4(o.imm); }
    else if (o.type == OT_TEMP) { auto it = tmp.find(o.index[0]); if (o.index_dim != 1 || it == tmp.end()) { err = "read of an undeclared temporary"; return false; } base = b.load(4, it->second.var); }
    else if (o.type == OT_INPUT) {
      auto it = in.find(o.index[0]); if (o.index_dim != 1 || it == in.end()) { err = "read of an undeclared input"; return false; }
      const Reg& r = it->second; uint32_t raw = b.load(r.width, r.var);
      if (r.width == 4) base = raw;
      else {
        uint32_t comp[4] = {zero, zero, zero, one};
        for (int k = 0; k < r.width; k++) comp[k] = r.width == 1 ? raw : b.extract(raw, k);
        base = b.construct4(comp[0], comp[1], comp[2], comp[3]);
      }
    }
    else if (o.type == OT_CBUFFER) {
      if (!cb_count || o.index_dim != 2 || !cb_regs.count(o.index[0])) { err = "read of an undeclared constant buffer"; return false; }
      const uint32_t slot_regs = cb_regs[o.index[0]], slot_off = cb_off[o.index[0]];
      uint32_t ic = 0;
      if (!o.has_rel) {
        if (o.index[1] >= slot_regs) { err = "constant buffer read past the declared size"; return false; }
        ic = b.UC(slot_off + o.index[1]);
      } else {
        auto it = tmp.find(o.rel_reg); if (it == tmp.end()) { err = "relative constant buffer index uses an undeclared temporary"; return false; }
        uint32_t tv = b.load(4, it->second.var), raw = b.extract(tv, o.rel_comp), u = b.cast(t_uint, raw);
        uint32_t sum = b.id(); b.put(b.body, sOpIAdd, {t_uint, sum, u, b.UC(slot_off + o.index[1])});
        uint32_t lim = b.UC(cb_count - 1), lt = b.id();   // clamp to the packed array (no out-of-bounds reads)
        b.put(b.body, 176, {b.BOOL1(), lt, sum, lim});               // OpULessThan
        ic = b.id(); b.put(b.body, sOpSelect, {t_uint, ic, lt, sum, lim});
      }
      uint32_t ch = b.id(); b.put(b.body, sOpAccessChain, {b.P(cb_ubo ? 2 : 9, 4), ch, cb_var, b.id_zero_uint(t_uint), ic});
      base = b.load(4, ch);
    }
    else if (o.type == OT_OUTPUT) { auto it = out.find(o.index[0]); if (o.index_dim != 1 || it == out.end()) { err = "read of an undeclared output"; return false; } base = b.load(4, it->second.var); }
    else { err = "unsupported source operand type " + std::to_string(o.type); return false; }
    if (o.ncomp == 2 && (o.selmode == SEL_SWIZZLE || o.selmode == SEL_SELECT1) && o.type != OT_IMM32) {
      bool ident = o.swizzle[0] == 0 && o.swizzle[1] == 1 && o.swizzle[2] == 2 && o.swizzle[3] == 3;
      if (!ident) base = b.shuffle(4, base, base, {o.swizzle[0], o.swizzle[1], o.swizzle[2], o.swizzle[3]});
    } else if (o.ncomp == 2 && o.selmode == SEL_MASK && o.type != OT_IMM32) { err = "mask used on a source operand"; return false; }
    if (o.abs) base = b.gl(4, GL_FAbs, {base});
    if (o.neg) base = b.un(sOpFNegate, 4, base);
    v = base; return true;
  };
  auto store_dst = [&](const Operand& o, uint32_t v) -> bool {
    uint32_t var = 0;
    if (o.type == OT_TEMP) { auto it = tmp.find(o.index[0]); if (it == tmp.end()) { err = "write to an undeclared temporary"; return false; } var = it->second.var; }
    else if (o.type == OT_OUTPUT) { auto it = out.find(o.index[0]); if (it == out.end()) { err = "write to an undeclared output"; return false; } var = it->second.var; }
    else { err = "unsupported destination operand type"; return false; }
    if (o.ncomp != 2 || o.selmode != SEL_MASK || o.index_dim != 1) { err = "destination must be a masked 4-component register"; return false; }
    if (o.mask == 0) return true;
    if (o.mask != 0xF) {
      uint32_t d = b.load(4, var); std::vector<uint32_t> idx;
      for (uint32_t k = 0; k < 4; k++) idx.push_back((o.mask & (1u << k)) ? 4 + k : k);
      v = b.shuffle(4, d, v, idx);
    }
    b.store(var, v); return true;
  };
  if (p.instructions.empty() || p.instructions.back().opcode != OP_RET) return fail("shader must end with ret");
  struct Ctl { int kind; uint32_t merge, els, cont; bool has_else; };   // kind 0 = if, 1 = loop
  std::vector<Ctl> stk;
  auto new_block = [&]() { b.put(b.body, sOpLabel, {b.id()}); };   // code after a terminator lands in a fresh (unreachable) block
  auto epilogue = [&]() {
    if (!pixel && vulkan_flip_y) {
      for (auto& kv : out) if (kv.second.builtin) {
        const uint32_t fl[4] = {fbits(1), fbits(-1), fbits(1), fbits(1)};
        uint32_t fc = b.C4(fl), v = b.load(4, kv.second.var);
        b.store(kv.second.var, b.bin(sOpFMul, 4, v, fc));
      }
    }
  };
  // scalar condition from an operand: bit pattern != 0 (nz) or == 0 (z)
  auto cond_of = [&](const Operand& o, bool nz, uint32_t& c) -> bool {
    uint32_t v = 0; if (!load_src(o, v)) return false;
    uint32_t f = b.extract(v, 0), u = b.cast(b.U1(), f), zero = b.UC(0);
    c = b.id(); b.put(b.body, nz ? sOpINotEqual : sOpIEqual, {b.BOOL1(), c, u, zero}); return true;
  };
  for (size_t ii = 0; ii < p.instructions.size(); ii++) {
    const Instruction& I = p.instructions[ii];
    if ((I.opcode >= OP_DCL_CONSTANT_BUFFER && I.opcode <= OP_DCL_GLOBAL_FLAGS) || I.opcode == OP_DCL_RESOURCE) continue;   // declarations handled above
    const bool is_last = ii + 1 == p.instructions.size();
    switch (I.opcode) {
      case OP_RET:
        epilogue();
        if (is_last && stk.empty()) continue;
        b.put(b.body, sOpReturn, {}); new_block(); continue;
      case OP_RETC: case OP_BREAKC: case OP_CONTINUEC: case OP_DISCARD: {
        const bool nz = (I.control & 0x80) != 0;
        uint32_t c = 0; if (!cond_of(I.ops[0], nz, c)) return fail(err);
        uint32_t target = 0;
        if (I.opcode == OP_BREAKC || I.opcode == OP_CONTINUEC) {
          for (size_t k = stk.size(); k-- > 0;) if (stk[k].kind == 1) { target = I.opcode == OP_BREAKC ? stk[k].merge : stk[k].cont; break; }
          if (!target) return fail("break/continue outside a loop");
        }
        if (I.opcode == OP_DISCARD && !pixel) return fail("discard in a vertex shader");
        uint32_t t = b.id(), m = b.id();
        b.put(b.body, sOpSelectionMerge, {m, 0}); b.put(b.body, sOpBranchConditional, {c, t, m}); b.put(b.body, sOpLabel, {t});
        if (I.opcode == OP_RETC) { epilogue(); b.put(b.body, sOpReturn, {}); }
        else if (I.opcode == OP_DISCARD) b.put(b.body, sOpKill, {});
        else b.put(b.body, sOpBranch, {target});
        b.put(b.body, sOpLabel, {m}); continue; }
      case OP_BREAK: case OP_CONTINUE: {
        uint32_t target = 0;
        for (size_t k = stk.size(); k-- > 0;) if (stk[k].kind == 1) { target = I.opcode == OP_BREAK ? stk[k].merge : stk[k].cont; break; }
        if (!target) return fail("break/continue outside a loop");
        b.put(b.body, sOpBranch, {target}); new_block(); continue; }
      case OP_IF: {
        uint32_t c = 0; if (!cond_of(I.ops[0], (I.control & 0x80) != 0, c)) return fail(err);
        Ctl k{0, b.id(), b.id(), 0, false}; uint32_t t = b.id();
        b.put(b.body, sOpSelectionMerge, {k.merge, 0}); b.put(b.body, sOpBranchConditional, {c, t, k.els}); b.put(b.body, sOpLabel, {t});
        stk.push_back(k); continue; }
      case OP_ELSE: {
        if (stk.empty() || stk.back().kind != 0 || stk.back().has_else) return fail("else without a matching if");
        b.put(b.body, sOpBranch, {stk.back().merge}); b.put(b.body, sOpLabel, {stk.back().els}); stk.back().has_else = true; continue; }
      case OP_ENDIF: {
        if (stk.empty() || stk.back().kind != 0) return fail("endif without a matching if");
        Ctl k = stk.back(); stk.pop_back();
        b.put(b.body, sOpBranch, {k.merge});
        if (!k.has_else) { b.put(b.body, sOpLabel, {k.els}); b.put(b.body, sOpBranch, {k.merge}); }
        b.put(b.body, sOpLabel, {k.merge}); continue; }
      case OP_LOOP: {
        if (stk.size() > 64) return fail("control flow nested too deeply");
        Ctl k{1, b.id(), 0, b.id(), false}; uint32_t header = b.id(), body = b.id();
        b.put(b.body, sOpBranch, {header}); b.put(b.body, sOpLabel, {header});
        b.put(b.body, sOpLoopMerge, {k.merge, k.cont, 0}); b.put(b.body, sOpBranch, {body}); b.put(b.body, sOpLabel, {body});
        k.els = header; stk.push_back(k); continue; }
      case OP_ENDLOOP: {
        if (stk.empty() || stk.back().kind != 1) return fail("endloop without a matching loop");
        Ctl k = stk.back(); stk.pop_back();
        b.put(b.body, sOpBranch, {k.cont}); b.put(b.body, sOpLabel, {k.cont}); b.put(b.body, sOpBranch, {k.els});
        b.put(b.body, sOpLabel, {k.merge}); continue; }
      default: break;
    }
    if (I.opcode == OP_SAMPLE || I.opcode == OP_SAMPLE_L) {
      if (!have_tex) return fail("sample without a declared texture");
      const Operand& rs = I.ops[2]; const Operand& sp = I.ops[3];
      if (rs.type != OT_RESOURCE || rs.index_dim != 1 || !tex_decl.count(rs.index[0])) return fail("sample: texture must be a declared t0..t3");
      if (sp.type != OT_SAMPLER || sp.index_dim != 1 || sp.index[0] > 3) return fail("sample: sampler must be s0..s3");
      if (R.tex_sampler[rs.index[0]] >= 0 && R.tex_sampler[rs.index[0]] != (int)sp.index[0]) return fail("sample: a texture sampled with two different samplers is not supported");
      if (!smp_decl.count(sp.index[0])) return fail("sample: sampler is not declared");
      R.tex_sampler[rs.index[0]] = (int)sp.index[0];
      uint32_t addr = 0, lod = 0;
      if (!load_src(I.ops[1], addr)) return fail(err + " (sample address)");
      if (I.opcode == OP_SAMPLE_L) {
        uint32_t lv = 0; if (!load_src(I.ops[4], lv)) return fail(err + " (sample lod)");
        lod = b.extract(lv, I.ops[4].swizzle[0]);
      }
      uint32_t uv = b.shuffle(2, addr, addr, {0, 1});
      uint32_t si = b.id(); b.put(b.body, sOpLoad, {t_simg, si, tex_var[rs.index[0]]});
      uint32_t res = b.id();
      if (I.opcode == OP_SAMPLE) b.put(b.body, sOpImageSampleImplicitLod, {b.T(4), res, si, uv});
      else b.put(b.body, sOpImageSampleExplicitLod, {b.T(4), res, si, uv, 2u, lod});
      bool ident = rs.swizzle[0] == 0 && rs.swizzle[1] == 1 && rs.swizzle[2] == 2 && rs.swizzle[3] == 3;
      if (rs.ncomp == 2 && rs.selmode != SEL_MASK && !ident) res = b.shuffle(4, res, res, {rs.swizzle[0], rs.swizzle[1], rs.swizzle[2], rs.swizzle[3]});
      if (I.saturate) { const uint32_t z4[4] = {0, 0, 0, 0}, o4[4] = {fbits(1), fbits(1), fbits(1), fbits(1)};
        uint32_t cz = b.C4(z4), co = b.C4(o4); res = b.gl(4, GL_FClamp, {res, cz, co}); }
      if (!store_dst(I.ops[0], res)) return fail(err + " (sample)");
      continue;
    }
    std::vector<uint32_t> s(I.ops.size());
    for (size_t k = 1; k < I.ops.size(); k++) if (!load_src(I.ops[k], s[k])) return fail(err + " (opcode " + std::to_string(I.opcode) + ")");
    uint32_t r = 0;
    switch (I.opcode) {
      case OP_MOV: r = s[1]; break;
      case OP_ADD: r = b.bin(sOpFAdd, 4, s[1], s[2]); break;
      case OP_MUL: r = b.bin(sOpFMul, 4, s[1], s[2]); break;
      case OP_DIV: r = b.bin(sOpFDiv, 4, s[1], s[2]); break;
      case OP_MAD: r = b.bin(sOpFAdd, 4, b.bin(sOpFMul, 4, s[1], s[2]), s[3]); break;
      case OP_MIN: r = b.gl(4, GL_FMin, {s[1], s[2]}); break;
      case OP_MAX: r = b.gl(4, GL_FMax, {s[1], s[2]}); break;
      case OP_SQRT: r = b.gl(4, GL_Sqrt, {s[1]}); break;
      case OP_RSQ: r = b.gl(4, GL_InvSqrt, {s[1]}); break;
      case OP_FRC: r = b.gl(4, GL_Fract, {s[1]}); break;
      case OP_EXP: r = b.gl(4, GL_Exp2, {s[1]}); break;
      case OP_LOG: r = b.gl(4, GL_Log2, {s[1]}); break;
      case OP_DP2: case OP_DP3: case OP_DP4: {
        int w = I.opcode == OP_DP2 ? 2 : I.opcode == OP_DP3 ? 3 : 4; uint32_t a = s[1], c2 = s[2];
        if (w < 4) { std::vector<uint32_t> idx; for (int k = 0; k < w; k++) idx.push_back(k); a = b.shuffle(w, s[1], s[1], idx); c2 = b.shuffle(w, s[2], s[2], idx); }
        uint32_t d = b.bin(sOpDot, 1, a, c2); r = b.construct4(d, d, d, d); break; }
      case OP_EQ: case OP_NE: case OP_LT: case OP_GE: case OP_IEQ: case OP_INE: case OP_ILT: case OP_IGE: {
        uint32_t a = s[1], c2 = s[2], op = 0;
        const bool isint = I.opcode >= OP_IEQ && I.opcode != OP_NE && I.opcode != OP_LT && I.opcode != OP_GE && I.opcode != OP_EQ;
        if (isint) { a = b.cast(b.I4(), a); c2 = b.cast(b.I4(), c2); }
        switch (I.opcode) {
          case OP_EQ: op = sOpFOrdEqual; break; case OP_NE: op = sOpFUnordNotEqual; break; case OP_LT: op = sOpFOrdLessThan; break;
          case OP_GE: op = sOpFOrdGreaterThanEqual; break; case OP_IEQ: op = sOpIEqual; break; case OP_INE: op = sOpINotEqual; break;
          case OP_ILT: op = sOpSLessThan; break; default: op = sOpSGreaterThanEqual; break;
        }
        uint32_t m = b.id(); b.put(b.body, op, {b.BOOL4(), m, a, c2});
        uint32_t ones = b.UC4(0xFFFFFFFFu), zeros = b.UC4(0), sel = b.id();
        b.put(b.body, sOpSelect, {b.U4(), sel, m, ones, zeros}); r = b.cast(b.T(4), sel); break; }
      case OP_MOVC: {
        uint32_t cu = b.cast(b.U4(), s[1]), m = b.id(); b.put(b.body, sOpINotEqual, {b.BOOL4(), m, cu, b.UC4(0)});
        r = b.id(); b.put(b.body, sOpSelect, {b.T(4), r, m, s[2], s[3]}); break; }
      case OP_IADD: { uint32_t x = b.cast(b.I4(), s[1]), y = b.cast(b.I4(), s[2]); r = b.cast(b.T(4), b.bin(sOpIAdd, 4, x, y) ); break; }
      case OP_INEG: { uint32_t x = b.cast(b.I4(), s[1]); uint32_t n = b.id(); b.put(b.body, sOpSNegate, {b.I4(), n, x}); r = b.cast(b.T(4), n); break; }
      case OP_FTOI: { uint32_t n = b.id(); b.put(b.body, sOpConvertFToS, {b.I4(), n, s[1]}); r = b.cast(b.T(4), n); break; }
      case OP_ITOF: { uint32_t x = b.cast(b.I4(), s[1]); r = b.id(); b.put(b.body, sOpConvertSToF, {b.T(4), r, x}); break; }
      case OP_AND: case OP_OR: case OP_XOR: {
        uint32_t x = b.cast(b.U4(), s[1]), y = b.cast(b.U4(), s[2]), n = b.id();
        b.put(b.body, I.opcode == OP_AND ? sOpBitwiseAnd : I.opcode == OP_OR ? sOpBitwiseOr : sOpBitwiseXor, {b.U4(), n, x, y}); r = b.cast(b.T(4), n); break; }
      case OP_NOT: { uint32_t x = b.cast(b.U4(), s[1]), n = b.id(); b.put(b.body, sOpNot, {b.U4(), n, x}); r = b.cast(b.T(4), n); break; }
      default: return fail("unsupported opcode " + std::to_string(I.opcode));
    }
    if (I.saturate) { const uint32_t z4[4] = {fbits(0), fbits(0), fbits(0), fbits(0)}, o4[4] = {fbits(1), fbits(1), fbits(1), fbits(1)};
      uint32_t cz = b.C4(z4), co = b.C4(o4); r = b.gl(4, GL_FClamp, {r, cz, co}); }
    if (!store_dst(I.ops[0], r)) return fail(err + " (opcode " + std::to_string(I.opcode) + ")");
  }
  if (!stk.empty()) return fail("unterminated if or loop");

  // entry point + execution mode
  { std::vector<uint32_t> e = {pixel ? 4u : 0u, main_id}; e.push_back(0x6e69616d); e.push_back(0);   // "main"
    e.insert(e.end(), iface.begin(), iface.end()); b.putv(b.entry, sOpEntryPoint, e); }
  if (pixel) b.put(b.modes, sOpExecutionMode, {main_id, 7});   // OriginUpperLeft
  std::vector<uint32_t> fn;
  b.put(fn, sOpFunction, {b.t_void, main_id, 0, b.t_fn});
  b.put(fn, sOpLabel, {label});
  fn.insert(fn.end(), tmp_decl.begin(), tmp_decl.end());
  fn.insert(fn.end(), b.body.begin(), b.body.end());
  b.put(fn, sOpReturn, {});
  b.put(fn, sOpFunctionEnd, {});

  std::vector<uint32_t>& W = R.words;
  W = {0x07230203, 0x00010000, 0, b.next, 0};
  for (auto* v : {&b.caps, &b.ext, &b.mem, &b.entry, &b.modes, &b.decor, &b.globals, &fn}) W.insert(W.end(), v->begin(), v->end());
  R.ok = true; return R;
}
}}
