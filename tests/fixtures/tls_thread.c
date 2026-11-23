/* Anyxbox fixture: CreateThread TLS callbacks and per-thread data, built with MinGW. */
#include <windows.h>
#include <stdio.h>
static volatile LONG attaches, detaches;
static __thread int counter = 41;
static __thread int zeroed;
static void NTAPI callback(PVOID module, DWORD reason, PVOID reserved) {
  (void)module; (void)reserved;
  if (reason == DLL_THREAD_ATTACH) ++attaches;
  if (reason == DLL_THREAD_DETACH) ++detaches;
}
PIMAGE_TLS_CALLBACK tls_thread_callback __attribute__((section(".CRT$XLB"), used)) = callback;
static DWORD WINAPI worker(LPVOID unused) {
  (void)unused;
  if (counter != 41 || zeroed != 0) return 3;
  counter = 77; zeroed = 5;
  return 7;
}
int main(void) {
  counter = 99;
  for (int i = 0; i < 2; ++i) {
    HANDLE h = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (!h || WaitForSingleObject(h, 5000) != WAIT_OBJECT_0) return 4;
    DWORD code = 0;
    if (!GetExitCodeThread(h, &code) || code != 7) return 5;
    CloseHandle(h);
  }
  printf("attach=%ld detach=%ld main=%d zero=%d\n", attaches, detaches, counter, zeroed);
  return attaches == 2 && detaches == 2 && counter == 99 && zeroed == 0 ? 0 : 6;
}
