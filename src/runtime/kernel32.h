// Anyxbox kernel32 subset: host implementations bound to the import registry. GPL-2.0.
// Functions use the Windows x64 calling convention (Linux x86-64 hosts only for now).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "syslayer/import_registry.h"
#include "syslayer/win32_file.h"
#include "syslayer/win32_memory.h"
#include "syslayer/win32_sync.h"
#include "syslayer/win32_thread.h"
namespace anyxbox {
struct Kernel32Context {
  HandleTable handles;
  VirtualMemory vm;
  std::unique_ptr<MappedFolder> folder;   // null = file functions fail
  uint64_t image_base = 0;                // returned by GetModuleHandle(NULL)
  std::string* out = nullptr;             // captured stdout/stderr
  std::mutex cs_mutex;
  std::map<uintptr_t, std::unique_ptr<CriticalSection>> critical_sections;
};
// Registers: GetStdHandle, WriteFile, ReadFile, CreateFileA, CloseHandle, SetFilePointer, GetFileSize,
// GetModuleHandleA, VirtualAlloc, VirtualFree, CreateThread, GetExitCodeThread, WaitForSingleObject,
// CreateEventA, SetEvent, ResetEvent, Sleep, GetTickCount64, QueryPerformanceCounter,
// QueryPerformanceFrequency, Initialize/Enter/TryEnter/Leave/DeleteCriticalSection.
// The context must outlive every call into the registered functions.
// Returns false on hosts that cannot run Windows-ABI code.
bool kernel32_register(ImportRegistry& reg, Kernel32Context& ctx);
}
