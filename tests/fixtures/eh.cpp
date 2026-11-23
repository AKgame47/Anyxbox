// Anyxbox fixture: a MinGW C++ program that throws and catches. The exception goes through libgcc's SEH unwinder, which calls
// RtlLookupFunctionEntry, RtlVirtualUnwind, RtlUnwindEx and RaiseException. Expected output: "caught boom, then ok".
#include <cstdio>
#include <stdexcept>
struct Guard { ~Guard() { std::puts("destructor ran"); } };
static int depth(int n) { Guard g; if (n == 0) throw std::runtime_error("boom"); return depth(n - 1) + 1; }
int main() {
  try { depth(2); }
  catch (const std::exception& e) { std::printf("caught %s\n", e.what()); }
  std::puts("ok");
  return 0;
}
