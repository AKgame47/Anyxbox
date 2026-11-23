// Anyxbox first end-to-end run: map a PE, bind a tiny import set, call its entry point. GPL-2.0.
// Linux x86-64 host only. The program's machine code runs natively on the CPU.
// WARNING: this executes the binary with the host's privileges. Use only binaries you built
// yourself. The PNR sandbox is not wired in yet, so run_pe requires an explicit trust flag.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace anyxbox {
struct RunResult {
  bool loaded = false;
  bool ran = false;
  std::string error;
  uint32_t exit_code = 0;
  std::string output;                  // captured stdout/stderr writes
  std::vector<std::string> unresolved; // imports the tiny set does not cover
};
RunResult run_pe(const uint8_t* data, size_t size, bool i_trust_this_binary);
}
