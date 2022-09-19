#pragma once

#include <iostream>
#include <sstream>

#include "logging_internal.h"

// A quick and dirty implementation of LOG() inspired by Chromium's
// base/logging.h and glog but smaller (and lacking features and quality).

namespace core {
namespace logging {

const LogSeverity INFO = 0;
const LogSeverity WARNING = 1;
const LogSeverity ERROR = 2;
const LogSeverity FATAL = 3;

// Allows logging to stdout. LOG(FATAL) will abort() the program.
// Other logging severities are logged but do not affect behavior.
//
// Example:
//   LOG(INFO) << "This is a log to stdout";
#define LOG(severity)                                                        \
  ::core::logging::LogMessage(__FILE__, __LINE__, ::core::logging::severity) \
      .stream()

#define LOG_IF(severity, cond) LAZY_CHECK_STREAM(LOG(severity), !(cond))

// Creates a LOG(FATAL), crashing the program, if (expr) is false.
//
// Example:
//   CHECK(0 == 0) << "The universe is not right...";
#define CHECK(expr) \
  LAZY_CHECK_STREAM(LOG(FATAL), (expr)) << "CHECK(" << #expr << ") failed: "
// Operator extenstions to CHECK macro:
#define CHECK_EQ(lhs, rhs) CHECK(lhs == rhs)
#define CHECK_NE(lhs, rhs) CHECK(lhs != rhs)
#define CHECK_LT(lhs, rhs) CHECK(lhs < rhs)

// Indicates a point which should not be reached in code.
#define NOTREACHED() LOG(FATAL) << "NOTREACHED(): "

#define COLOR(value) "\u001b[38;5;" << #value << "m"
#define COLOR_RESET() "\u001b[0m"

}  // namespace logging
}  // namespace core