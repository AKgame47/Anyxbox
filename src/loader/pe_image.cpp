#include "loader/pe_image.h"
#include <cstring>
namespace anyxbox {
namespace {
struct Rd {
  const uint8_t* d; size_t n;
  bool ok(uint64_t off, uint64_t len) const { return off <= n && len <= n - off; }
  uint16_t u16(uint64_t o) const { uint16_t v; std::memcpy(&v, d + o, 2); return v; }
  uint32_t u32(uint64_t o) const { uint32_t v; std::memcpy(&v, d + o, 4); return v; }
  uint64_t u64(uint64_t o) const { uint64_t v; std::memcpy(&v, d + o, 8); return v; }
};
LoadedImage fail(LoadedImage r, const char* msg) { r.ok = false; r.error = msg; return r; }

// Read a NUL-terminated string from the mapped image, bounded.
bool cstr(const std::vector<uint8_t>& m, uint32_t rva, std::string& out) {
  out.clear();
  for (uint32_t i = rva; i < m.size() && i - rva < 512; i++) {
    if (m[i] == 0) return true;
    out.push_back((char)m[i]);
  }
  return false;
}
}  // namespace

LoadedImage load_pe_image(const uint8_t* data, size_t size, const ImportResolver& resolver) {
  LoadedImage r;
  Rd f{data, size};
  if (size < 0x40 || data[0] != 'M' || data[1] != 'Z') return fail(r, "bad DOS magic");
  uint32_t pe = f.u32(0x3c);
  if (!f.ok(pe, 24 + 112) || std::memcmp(data + pe, "PE\0\0", 4) != 0) return fail(r, "bad PE signature");
  uint64_t coff = pe + 4;
  if (f.u16(coff) != 0x8664) return fail(r, "not x86-64");
  uint16_t nsec = f.u16(coff + 2);
  uint16_t optsz = f.u16(coff + 16);
  uint64_t opt = coff + 20;
  if (optsz < 112 || !f.ok(opt, optsz)) return fail(r, "bad optional header size");
  if (f.u16(opt) != 0x20b) return fail(r, "not PE32+");
  r.entry_rva = f.u32(opt + 16);
  r.preferred_base = f.u64(opt + 24);
  uint32_t sec_align = f.u32(opt + 32);
  uint32_t image_size = f.u32(opt + 56);
  uint32_t hdr_size = f.u32(opt + 60);
  uint32_t ndirs = f.u32(opt + 108);
  if (sec_align == 0 || (sec_align & (sec_align - 1))) return fail(r, "bad section alignment");
  if (image_size == 0 || image_size > (1u << 30)) return fail(r, "bad SizeOfImage");
  if (hdr_size > image_size || hdr_size > size) return fail(r, "bad SizeOfHeaders");
  if (ndirs > 16) ndirs = 16;
  if (112 + (uint64_t)ndirs * 8 > optsz) return fail(r, "data directories overflow");
  uint64_t dirs = opt + 112;
  uint64_t sects = opt + optsz;
  if (!f.ok(sects, (uint64_t)nsec * 40)) return fail(r, "section table out of range");

  r.mem.assign(image_size, 0);
  std::memcpy(r.mem.data(), data, hdr_size);
  for (uint16_t i = 0; i < nsec; i++) {
    uint64_t s = sects + (uint64_t)i * 40;
    uint32_t vsize = f.u32(s + 8), va = f.u32(s + 12), rawsz = f.u32(s + 16), rawp = f.u32(s + 20);
    uint32_t copy = rawsz < vsize || vsize == 0 ? rawsz : vsize;
    if (copy == 0) continue;
    if (va > image_size || copy > image_size - va) return fail(r, "section exceeds image");
    if (!f.ok(rawp, copy)) return fail(r, "section raw data out of file");
    std::memcpy(r.mem.data() + va, data + rawp, copy);
  }
  r.actual_base = (uint64_t)(uintptr_t)r.mem.data();
  Rd m{r.mem.data(), r.mem.size()};
  auto dir = [&](int idx, uint32_t& rva, uint32_t& sz) {
    rva = sz = 0;
    if ((uint32_t)idx < ndirs) { rva = f.u32(dirs + idx * 8); sz = f.u32(dirs + idx * 8 + 4); }
  };

  // Base relocations (directory 5)
  uint32_t rrva, rsz; dir(5, rrva, rsz);
  if (rsz) {
    if (!m.ok(rrva, rsz)) return fail(r, "reloc directory out of range");
    int64_t delta = (int64_t)(r.actual_base - r.preferred_base);
    uint32_t off = 0;
    while (off + 8 <= rsz) {
      uint32_t page = m.u32(rrva + off), blk = m.u32(rrva + off + 4);
      if (blk < 8 || off + blk > rsz) return fail(r, "bad reloc block");
      for (uint32_t e = 8; e + 2 <= blk; e += 2) {
        uint16_t ent = m.u16(rrva + off + e);
        uint16_t type = ent >> 12; uint32_t at = page + (ent & 0xfff);
        if (type == 0) continue;
        if (type == 10) {
          if (!m.ok(at, 8)) return fail(r, "reloc target out of range");
          uint64_t v = m.u64(at) + (uint64_t)delta; std::memcpy(r.mem.data() + at, &v, 8);
        } else if (type == 3) {
          if (!m.ok(at, 4)) return fail(r, "reloc target out of range");
          uint32_t v = m.u32(at) + (uint32_t)delta; std::memcpy(r.mem.data() + at, &v, 4);
        } else return fail(r, "unsupported reloc type");
        r.reloc_count++;
      }
      off += blk;
    }
  }

  // Imports (directory 1)
  uint32_t irva, isz; dir(1, irva, isz);
  if (isz) {
    for (uint32_t d = irva;; d += 20) {
      if (!m.ok(d, 20)) return fail(r, "import descriptor out of range");
      uint32_t oft = m.u32(d), nameRva = m.u32(d + 12), ft = m.u32(d + 16);
      if (oft == 0 && nameRva == 0 && ft == 0) break;
      std::string dll;
      if (!cstr(r.mem, nameRva, dll)) return fail(r, "bad import dll name");
      uint32_t look = oft ? oft : ft;
      for (uint32_t k = 0;; k++) {
        uint32_t lp = look + k * 8, ip = ft + k * 8;
        if (!m.ok(lp, 8) || !m.ok(ip, 8)) return fail(r, "import thunk out of range");
        uint64_t th = m.u64(lp);
        if (th == 0) break;
        ImportRef ref{dll, "", 0, false, ip};
        if (th >> 63) { ref.by_ordinal = true; ref.ordinal = (uint16_t)(th & 0xffff); }
        else if (!cstr(r.mem, (uint32_t)th + 2, ref.name)) return fail(r, "bad import name");
        uint64_t addr = resolver ? resolver(dll, ref.name, ref.ordinal, ref.by_ordinal) : 0;
        if (!addr) {
          r.unresolved.push_back(dll + "!" + (ref.by_ordinal ? "#" + std::to_string(ref.ordinal) : ref.name));
        }
        std::memcpy(r.mem.data() + ip, &addr, 8);
        r.imports.push_back(ref);
      }
    }
  }
  r.ok = r.unresolved.empty();
  if (!r.ok) r.error = "unresolved imports";
  return r;
}
}  // namespace anyxbox
