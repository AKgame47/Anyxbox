// Anyxbox structured exceptions (Windows x64 table-based unwinding). GPL-2.0.
// Linux x86-64 hosts only. Software exceptions (RaiseException, RtlUnwind/RtlUnwindEx, RtlCaptureContext/RtlRestoreContext,
// RtlLookupFunctionEntry, RtlVirtualUnwind) and the C scope-table handler __C_specific_handler (try/except/finally in C).
// C++ exceptions of MinGW programs reach the same Rtl* functions through the program's own libgcc.
// Not done: hardware faults (access violation, divide by zero) are not turned into guest exceptions yet.
#pragma once
#include <cstdint>
#include <functional>
#include "syslayer/import_registry.h"
namespace anyxbox {
// Tell the unwinder where a loaded image's .pdata lives. Rva values come from the PE exception directory.
void seh_register_image(uint64_t base, uint32_t size, uint32_t pdata_rva, uint32_t pdata_size);
void seh_unregister_image(uint64_t base);
// Called with the exception code when no guest handler takes an exception (the program is then ended).
void seh_set_unhandled(std::function<void(uint32_t)> fn);
// kernel32/ntdll: RaiseException, RtlCaptureContext, RtlRestoreContext, RtlLookupFunctionEntry, RtlVirtualUnwind, RtlUnwind, RtlUnwindEx.
// msvcrt/ucrtbase/vcruntime140: __C_specific_handler. Returns false on hosts that cannot run Windows-ABI code.
bool seh_register(ImportRegistry& reg);
}
