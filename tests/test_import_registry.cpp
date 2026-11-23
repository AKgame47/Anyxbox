#include "syslayer/import_registry.h"
#include <cstdio>
int main() {
  anyxbox::ImportRegistry r;
  r.add("KERNEL32.dll", "ExitProcess", 0x10);
  r.add_ordinal("d3d11.dll", 7, 0x20);
  if (r.resolve("kernel32.DLL", "ExitProcess", 0, false) != 0x10) { std::puts("FAIL 1"); return 1; }
  if (r.resolve("d3d11.dll", "", 7, true) != 0x20) { std::puts("FAIL 2"); return 1; }
  if (r.resolve("kernel32.dll", "Nope", 0, false) != 0) { std::puts("FAIL 3"); return 1; }
  std::puts("ok"); return 0;
}
