// Anyxbox kernel32 subset: host implementations bound to the import registry. GPL-2.0.
// Functions use the Windows x64 calling convention (Linux x86-64 hosts only for now).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
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
  uint64_t image_size = 0;                // mapped size of the main image; VirtualProtect only changes pages inside it or inside VirtualAlloc regions
  std::string* out = nullptr;             // captured stdout/stderr
  const ImportRegistry* registry = nullptr;  // set by kernel32_register: LoadLibrary/GetProcAddress resolve against it
  std::mutex mod_mutex;
  std::vector<std::string> modules;          // dll names handed out as module handles (index + 1)
  std::mutex cs_mutex;
  std::map<uintptr_t, std::unique_ptr<CriticalSection>> critical_sections;
};
// Registers: GetStdHandle, WriteFile, ReadFile, CreateFileA, CloseHandle, SetFilePointer, GetFileSize,
// GetModuleHandleA, VirtualAlloc, VirtualFree, CreateThread, GetExitCodeThread, WaitForSingleObject,
// CreateEventA, SetEvent, ResetEvent, Sleep, GetTickCount64, QueryPerformanceCounter,
// QueryPerformanceFrequency, Initialize/Enter/TryEnter/Leave/DeleteCriticalSection.
// LoadLibraryA/W, GetModuleHandleA/W, GetProcAddress and FreeLibrary only know the DLLs that have functions registered
// here (a "module" is the registered set; no file is ever loaded from disk). Unknown names fail with ERROR_MOD_NOT_FOUND.
// The context must outlive every call into the registered functions.
// Returns false on hosts that cannot run Windows-ABI code.
bool kernel32_register(ImportRegistry& reg, Kernel32Context& ctx);
}
