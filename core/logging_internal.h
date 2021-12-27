#pragma once

#include <iostream>
#include <sstream>

#include "logging_internal.h"

namespace core {
namespace logging {

typedef int LogSeverity;

// Represents a single log message to stdout
class LogMessage final {
 public:
  LogMessage(const char* filename, int line_number, LogSeverity severity);
  ~LogMessage();

  std::ostream& stream() { return stream_; }

 protected:
  // Disallow copy and assign:
  LogMessage(const LogMessage&) = delete;
  LogMessage& operator=(LogMessage&) = delete;

 private:
  const char* filename_;
  const int line_number_;
  const LogSeverity severity_;

  std::ostringstream stream_;
};

// Helper class used to explicitly ignore an std::ostream
class VoidifyStream final {
 public:
  // This operator has lower precedence than << but higher than ?:
  void operator&(std::ostream&) {}
};

// Helper macro which avoids evaluating the arguents to a stream if the
// condition is false.
#define LAZY_CHECK_STREAM(stream, condition) \
  (condition) ? (void)0 : ::core::logging::VoidifyStream() & (stream)

}  // namespace logging
}  // namespace core