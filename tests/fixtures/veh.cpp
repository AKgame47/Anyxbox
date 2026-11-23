// Anyxbox fixture: vectored exception handlers and the unhandled-exception filter, through the real MinGW headers.
// Expected output: "veh saw e0001234", "after raise", "filter saw e0002222", "after second raise", "removed 1", "ok".
#include <windows.h>
#include <cstdio>
static LONG WINAPI veh(EXCEPTION_POINTERS* p) {
  if (p->ExceptionRecord->ExceptionCode != 0xE0001234u) return EXCEPTION_CONTINUE_SEARCH;
  std::printf("veh saw %lx\n", p->ExceptionRecord->ExceptionCode);
  return EXCEPTION_CONTINUE_EXECUTION;
}
static LONG WINAPI filter(EXCEPTION_POINTERS* p) {
  std::printf("filter saw %lx\n", p->ExceptionRecord->ExceptionCode);
  return EXCEPTION_CONTINUE_EXECUTION;
}
int main() {
  void* h = AddVectoredExceptionHandler(1, veh);
  SetUnhandledExceptionFilter(filter);
  RaiseException(0xE0001234u, 0, 0, nullptr);
  std::puts("after raise");
  RaiseException(0xE0002222u, 0, 0, nullptr);
  std::puts("after second raise");
  std::printf("removed %lu\n", RemoveVectoredExceptionHandler(h));
  std::puts("ok");
  return 0;
}
