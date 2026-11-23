// Anyxbox thread environment block: guest code from a C runtime reads the TEB through the gs register
// (gs:[0x30] self, gs:[0x08] stack base, gs:[0x60] PEB). Linux x86-64 only. GPL-2.0.
#pragma once
#include <cstdint>
namespace anyxbox {
// Gives the calling thread a TEB (and the process a shared PEB) and points the gs base at it.
// Returns false when the kernel refuses (arch_prctl) or on other hosts. Call again on the same thread to update the image base.
bool teb_install(uint64_t image_base);
// Puts the gs base back to what it was before the first teb_install on this thread.
void teb_restore();
// Sets the image's TLS template for every thread that installs a TEB after this call: raw bytes followed by
// zero_fill zero bytes. gs:[0x58][0] then points at a fresh per-thread copy. Pass null/0/0 to clear.
void teb_set_tls_template(const uint8_t* raw, uint32_t raw_size, uint32_t zero_fill);
// Address of the calling thread's TEB (null before teb_install).
const void* teb_current();
}
