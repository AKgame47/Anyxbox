// Hand-built DXBC for tests. Tokens are assembled from the documented bit fields (not copied from compiler output).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace dxb {
typedef std::vector<uint32_t> Words;
enum { T_TEMP = 0, T_INPUT = 1, T_OUTPUT = 2, T_IMM = 4, T_CB = 8 };
inline uint32_t fb(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
inline Words dst(int type, uint32_t reg, int mask) { return {2u | ((uint32_t)mask << 4) | ((uint32_t)type << 12) | (1u << 20), reg}; }
inline uint32_t swz(int a, int b, int c, int d) { return (uint32_t)(a | b << 2 | c << 4 | d << 6); }
inline Words src(int type, uint32_t reg, uint32_t sw = swz(0, 1, 2, 3), bool neg = false, bool abs = false) {
  uint32_t t = 2u | (1u << 2) | (sw << 4) | ((uint32_t)type << 12) | (1u << 20);
  Words w;
  if (neg || abs) { t |= 1u << 31; w = {t, 1u | ((neg && abs ? 3u : abs ? 2u : 1u) << 6), reg}; }
  else w = {t, reg};
  return w;
}
inline Words imm4(float a, float b, float c, float d) { return {2u | (1u << 2) | (swz(0, 1, 2, 3) << 4) | (4u << 12), fb(a), fb(b), fb(c), fb(d)}; }
inline Words imm1(float a) { return {1u | (4u << 12), fb(a)}; }
inline Words ins(uint32_t op, std::vector<Words> ops, bool sat = false, uint32_t control = 0, std::vector<uint32_t> tail = {}) {
  Words body;
  for (auto& o : ops) body.insert(body.end(), o.begin(), o.end());
  body.insert(body.end(), tail.begin(), tail.end());
  Words w; w.push_back(op | (control << 11) | (sat ? 0x2000u : 0) | ((uint32_t)(body.size() + 1) << 24));
  w.insert(w.end(), body.begin(), body.end()); return w;
}
inline Words dcl_temps(uint32_t n) { return {104u | (2u << 24), n}; }
struct Sig { const char* name; uint32_t idx, sysval, reg; uint8_t mask; };
inline std::vector<uint8_t> sig_chunk(const std::vector<Sig>& s) {
  std::vector<uint8_t> b; auto p32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); };
  p32((uint32_t)s.size()); p32(8);
  uint32_t off = 8 + 24 * (uint32_t)s.size();
  std::vector<uint32_t> offs;
  for (auto& e : s) { offs.push_back(off); off += (uint32_t)std::strlen(e.name) + 1; }
  for (size_t i = 0; i < s.size(); i++) {
    p32(offs[i]); p32(s[i].idx); p32(s[i].sysval); p32(3); p32(s[i].reg);
    b.push_back(s[i].mask); b.push_back(s[i].mask); b.push_back(0); b.push_back(0);
  }
  for (auto& e : s) { for (const char* q = e.name; *q; q++) b.push_back((uint8_t)*q); b.push_back(0); }
  while (b.size() % 4) b.push_back(0);
  return b;
}
inline std::vector<uint8_t> container(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& chunks) {
  std::vector<uint8_t> o(32 + 4 * chunks.size(), 0);
  std::memcpy(o.data(), "DXBC", 4);
  auto put = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; i++) o[at + i] = (uint8_t)(v >> (8 * i)); };
  put(20, 1); put(28, (uint32_t)chunks.size());
  for (size_t i = 0; i < chunks.size(); i++) {
    put(32 + 4 * i, (uint32_t)o.size());
    o.insert(o.end(), {(uint8_t)chunks[i].first[0], (uint8_t)chunks[i].first[1], (uint8_t)chunks[i].first[2], (uint8_t)chunks[i].first[3], 0, 0, 0, 0});
    put(o.size() - 4, (uint32_t)chunks[i].second.size());
    o.insert(o.end(), chunks[i].second.begin(), chunks[i].second.end());
  }
  put(24, (uint32_t)o.size());
  return o;
}
// type: 0 pixel, 1 vertex
inline std::vector<uint8_t> shader(uint32_t type, const std::vector<Sig>& isgn, const std::vector<Sig>& osgn, const std::vector<Words>& code) {
  Words w = {(type << 16) | 0x40, 0};
  for (auto& i : code) w.insert(w.end(), i.begin(), i.end());
  w[1] = (uint32_t)w.size();
  std::vector<uint8_t> sh(w.size() * 4); std::memcpy(sh.data(), w.data(), sh.size());
  return container({{"ISGN", sig_chunk(isgn)}, {"OSGN", sig_chunk(osgn)}, {"SHDR", sh}});
}
const uint32_t OP_MOV = 54, OP_MUL = 56, OP_MAD = 50, OP_DP3 = 16, OP_RET = 62, OP_ADD = 0;
// the two test shaders from the batch plan
inline std::vector<uint8_t> vs_scale_half() {
  return shader(1, {{"POSITION", 0, 0, 0, 0xF}, {"COLOR", 0, 0, 1, 0x7}}, {{"SV_Position", 0, 1, 0, 0xF}, {"COLOR", 0, 0, 1, 0xF}},
    {ins(95, {dst(T_INPUT, 0, 0xF)}), ins(95, {dst(T_INPUT, 1, 0x7)}),
     ins(103, {dst(T_OUTPUT, 0, 0xF)}, false, 0, {1}), ins(101, {dst(T_OUTPUT, 1, 0xF)}), dcl_temps(1),
     ins(OP_MUL, {dst(T_TEMP, 0, 0xF), src(T_INPUT, 0), imm4(0.5f, 0.5f, 1, 1)}),
     ins(OP_MOV, {dst(T_OUTPUT, 0, 0xF), src(T_TEMP, 0)}),
     ins(OP_MOV, {dst(T_OUTPUT, 1, 0x7), src(T_INPUT, 1, swz(0, 1, 2, 0))}),
     ins(OP_MOV, {dst(T_OUTPUT, 1, 0x8), imm1(1)}), ins(OP_RET, {})});
}
inline std::vector<uint8_t> ps_luma() {
  return shader(0, {{"COLOR", 0, 0, 1, 0xF}}, {{"SV_Target", 0, 0, 0, 0xF}},
    {ins(98, {dst(T_INPUT, 1, 0xF)}, false, 2), ins(101, {dst(T_OUTPUT, 0, 0xF)}), dcl_temps(1),
     ins(OP_DP3, {dst(T_TEMP, 0, 0x1), src(T_INPUT, 1, swz(0, 1, 2, 0)), imm4(0.299f, 0.587f, 0.114f, 0)}),
     ins(OP_MAD, {dst(T_TEMP, 0, 0x1), src(T_TEMP, 0, swz(0, 0, 0, 0)), imm1(0.5f), imm1(0.25f)}),
     ins(OP_MOV, {dst(T_OUTPUT, 0, 0x7), src(T_TEMP, 0, swz(0, 0, 0, 0))}, true),
     ins(OP_MOV, {dst(T_OUTPUT, 0, 0x8), imm1(1)}), ins(OP_RET, {})});
}
// o0 = sat(v1 * 2 - 0.5), with a negated source and abs thrown in (mad_sat o0, v1, l(2), -l(0.5)) is not used; this one uses neg/abs separately
inline std::vector<uint8_t> ps_sat_affine() {
  return shader(0, {{"COLOR", 0, 0, 1, 0xF}}, {{"SV_Target", 0, 0, 0, 0xF}},
    {ins(98, {dst(T_INPUT, 1, 0xF)}, false, 2), ins(101, {dst(T_OUTPUT, 0, 0xF)}), dcl_temps(1),
     ins(OP_MAD, {dst(T_OUTPUT, 0, 0xF), src(T_INPUT, 1), imm1(2.0f), imm1(-0.5f)}, true),
     ins(OP_RET, {})});
}
// o0 = |(-v1)| via neg then abs modifiers => equals v1 for non-negative colours; combined with 1 - ... not needed
inline std::vector<uint8_t> ps_negabs() {
  // r0 = -v1 ; o0 = abs(-r0) negated twice cancels: o0 = abs(r0) = v1 ; alpha forced to 1
  return shader(0, {{"COLOR", 0, 0, 1, 0xF}}, {{"SV_Target", 0, 0, 0, 0xF}},
    {ins(98, {dst(T_INPUT, 1, 0xF)}, false, 2), ins(101, {dst(T_OUTPUT, 0, 0xF)}), dcl_temps(1),
     ins(OP_MOV, {dst(T_TEMP, 0, 0xF), src(T_INPUT, 1, swz(0, 1, 2, 3), true)}),
     ins(OP_MOV, {dst(T_OUTPUT, 0, 0xF), src(T_TEMP, 0, swz(0, 1, 2, 3), false, true)}),
     ins(OP_RET, {})});
}
}  // namespace dxb
