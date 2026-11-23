// Status/Error result types shared by the Vulkan backend. GPL-2.0.
// Extracted unchanged from AnythingPS5 core/runtime.hpp (the rest of that header is not needed here).
#pragma once
#include <string>
#include <utility>
namespace pnr {
struct Error {
  int code = 0;
  std::string subsystem;
  std::string message;
};

// Uniform success/failure result. No silent dummy successes (rule 15).
struct Status {
  bool ok = true;
  Error error{};
  static Status Ok() { return Status{}; }
  static Status Fail(int code, std::string subsystem, std::string message) {
    Status s;
    s.ok = false;
    s.error = Error{code, std::move(subsystem), std::move(message)};
    return s;
  }
  explicit operator bool() const { return ok; }
};
}  // namespace pnr
