#include "syslayer/import_registry.h"
#include <algorithm>
#include <cctype>
namespace anyxbox {
static std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
  return s;
}
std::string ImportRegistry::key(const std::string& dll, const std::string& n) { return lower(dll) + "!" + n; }
void ImportRegistry::add(const std::string& dll, const std::string& name, uint64_t a) { map_[key(dll, name)] = a; }
void ImportRegistry::add_ordinal(const std::string& dll, uint16_t o, uint64_t a) { map_[key(dll, "#" + std::to_string(o))] = a; }
uint64_t ImportRegistry::resolve(const std::string& dll, const std::string& name, uint16_t o, bool byo) const {
  auto it = map_.find(key(dll, byo ? "#" + std::to_string(o) : name));
  return it == map_.end() ? 0 : it->second;
}
bool ImportRegistry::has_dll(const std::string& dll) const {
  std::string pre = lower(dll) + "!";
  auto it = map_.lower_bound(pre);
  return it != map_.end() && it->first.compare(0, pre.size(), pre) == 0;
}
}
