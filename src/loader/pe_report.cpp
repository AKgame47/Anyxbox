#include "loader/pe_report.h"
#include <map>
namespace anyxbox {
std::string pe_report(const LoadedImage& img) {
  std::string s;
  s += img.ok ? "load: OK\n" : "load: FAILED (" + img.error + ")\n";
  if (img.image_size == 0) return s;
  s += "image size: " + std::to_string(img.image_size) + " bytes\n";
  s += "entry rva: " + std::to_string(img.entry_rva) + "\n";
  s += "relocations applied: " + std::to_string(img.reloc_count) + "\n";
  std::map<std::string, int> per;
  for (auto& i : img.imports) per[i.dll]++;
  for (auto& kv : per) s += "imports from " + kv.first + ": " + std::to_string(kv.second) + "\n";
  s += "unresolved: " + std::to_string(img.unresolved.size()) + "\n";
  for (auto& u : img.unresolved) s += "  missing " + u + "\n";
  return s;
}
}
