// Anyxbox import registry: maps "dll!function" to host implementations. GPL-2.0.
#pragma once
#include <cstdint>
#include <map>
#include <string>
namespace anyxbox {
class ImportRegistry {
 public:
  void add(const std::string& dll, const std::string& name, uint64_t host_addr);
  void add_ordinal(const std::string& dll, uint16_t ordinal, uint64_t host_addr);
  uint64_t resolve(const std::string& dll, const std::string& name, uint16_t ordinal, bool by_ordinal) const;
  size_t size() const { return map_.size(); }
 private:
  static std::string key(const std::string& dll, const std::string& n);
  std::map<std::string, uint64_t> map_;
};
}
