// Anyxbox mapped-folder file API: CreateFile/ReadFile/WriteFile style, jailed under a root. GPL-2.0.
// Application-level path routing, not an OS sandbox.
#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include "syslayer/win32_sync.h"
namespace anyxbox {

enum FileMode { OPEN_EXISTING_RW_READ = 1, CREATE_WRITE_TRUNCATE = 2, OPEN_APPEND = 3 };

class MappedFolder {
 public:
  explicit MappedFolder(std::string root) : root_(std::move(root)) {}
  // Virtual path like "saves/a.sav" or "\\saves\\a.sav". Rejects "..", drive letters, empty paths, NUL.
  bool resolve(const std::string& vpath, std::string& host) const;
  // Returns a handle or 0xFFFFFFFF (INVALID_HANDLE_VALUE).
  uint32_t CreateFile(HandleTable& t, const std::string& vpath, FileMode mode) const;
  bool exists(const std::string& vpath) const;
 private:
  std::string root_;
};

class FileObject : public KernelObject {
 public:
  FileObject(std::string host, FileMode m);
  bool ok() const { return ok_; }
  bool read(void* buf, uint32_t n, uint32_t* got);
  bool write(const void* buf, uint32_t n, uint32_t* wrote);
  bool seek(int64_t off, int whence /*0 set 1 cur 2 end*/, uint64_t* newpos);
  bool size(uint64_t* sz);
 protected:
  bool signaled_locked() override { return true; }
  void consume_locked() override {}
 private:
  std::fstream f_;
  bool ok_ = false;
};
bool ReadFile(const HandleTable& t, uint32_t h, void* buf, uint32_t n, uint32_t* got);
bool WriteFile(const HandleTable& t, uint32_t h, const void* buf, uint32_t n, uint32_t* wrote);
bool SetFilePointer(const HandleTable& t, uint32_t h, int64_t off, int whence, uint64_t* newpos);
bool GetFileSize(const HandleTable& t, uint32_t h, uint64_t* sz);
}
