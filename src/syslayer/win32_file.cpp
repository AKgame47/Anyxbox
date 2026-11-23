#include "syslayer/win32_file.h"
#include <filesystem>
#include <memory>
namespace anyxbox {
namespace fs = std::filesystem;
bool MappedFolder::resolve(const std::string& vpath, std::string& host) const {
  if (vpath.empty() || vpath.find('\0') != std::string::npos) return false;
  std::string p = vpath;
  for (auto& c : p) if (c == '\\') c = '/';
  if (p.size() >= 2 && p[1] == ':') return false;
  std::string rel; size_t i = 0; bool any = false;
  while (i <= p.size()) {
    size_t j = p.find('/', i); if (j == std::string::npos) j = p.size();
    std::string part = p.substr(i, j - i);
    if (part == "..") return false;
    if (!part.empty() && part != ".") { rel += (any ? "/" : "") + part; any = true; }
    i = j + 1;
  }
  if (!any) return false;
  host = root_ + "/" + rel;
  return true;
}
bool MappedFolder::exists(const std::string& v) const {
  std::string h; return resolve(v, h) && fs::exists(h);
}
uint32_t MappedFolder::CreateFile(HandleTable& t, const std::string& vpath, FileMode mode) const {
  std::string h;
  if (!resolve(vpath, h)) return 0xFFFFFFFF;
  if (mode != OPEN_EXISTING_RW_READ) {
    std::error_code ec; fs::create_directories(fs::path(h).parent_path(), ec);
    if (ec) return 0xFFFFFFFF;
  }
  auto f = std::make_shared<FileObject>(h, mode);
  return f->ok() ? t.add(f) : 0xFFFFFFFF;
}
FileObject::FileObject(std::string host, FileMode m) {
  std::ios::openmode om = std::ios::binary;
  if (m == OPEN_EXISTING_RW_READ) om |= std::ios::in;
  else if (m == CREATE_WRITE_TRUNCATE) om |= std::ios::in | std::ios::out | std::ios::trunc;
  else om |= std::ios::out | std::ios::app;
  f_.open(host, om);
  ok_ = f_.is_open();
}
bool FileObject::read(void* b, uint32_t n, uint32_t* got) {
  std::lock_guard<std::mutex> l(m_);
  f_.clear(); f_.read((char*)b, n);
  if (got) *got = (uint32_t)f_.gcount();
  f_.clear(); return true;
}
bool FileObject::write(const void* b, uint32_t n, uint32_t* w) {
  std::lock_guard<std::mutex> l(m_);
  f_.write((const char*)b, n); f_.flush();
  if (w) *w = f_.good() ? n : 0;
  bool g = f_.good(); f_.clear(); return g;
}
bool FileObject::seek(int64_t off, int wh, uint64_t* np) {
  std::lock_guard<std::mutex> l(m_);
  if (wh < 0 || wh > 2) return false;
  f_.clear();
  f_.seekg(off, wh == 0 ? std::ios::beg : wh == 1 ? std::ios::cur : std::ios::end);
  f_.seekp(f_.tellg());
  if (!f_) { f_.clear(); return false; }
  if (np) *np = (uint64_t)f_.tellg();
  return true;
}
bool FileObject::size(uint64_t* sz) {
  std::lock_guard<std::mutex> l(m_);
  f_.clear(); auto cur = f_.tellg();
  f_.seekg(0, std::ios::end); auto end = f_.tellg(); f_.seekg(cur);
  if (end < 0) { f_.clear(); return false; }
  *sz = (uint64_t)end; return true;
}
static std::shared_ptr<FileObject> fo(const HandleTable& t, uint32_t h) {
  return std::dynamic_pointer_cast<FileObject>(t.get(h));
}
bool ReadFile(const HandleTable& t, uint32_t h, void* b, uint32_t n, uint32_t* g) { auto o = fo(t, h); return o && o->read(b, n, g); }
bool WriteFile(const HandleTable& t, uint32_t h, const void* b, uint32_t n, uint32_t* w) { auto o = fo(t, h); return o && o->write(b, n, w); }
bool SetFilePointer(const HandleTable& t, uint32_t h, int64_t off, int wh, uint64_t* np) { auto o = fo(t, h); return o && o->seek(off, wh, np); }
bool GetFileSize(const HandleTable& t, uint32_t h, uint64_t* sz) { auto o = fo(t, h); return o && o->size(sz); }
}
