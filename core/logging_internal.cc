#include "logging.h"
#include "logging_internal.h"

namespace core {
namespace logging {
namespace {

const std::string GetName(LogSeverity severity) {
#define NAME_STATEMENT(value) \
  case value:                 \
    return #value;
  switch (severity) {
    NAME_STATEMENT(INFO);
    NAME_STATEMENT(WARNING);
    NAME_STATEMENT(ERROR);
    NAME_STATEMENT(FATAL);
    default:
      break;
  }
  NOTREACHED();
  return {};
#undef NAME_STATEMENT
}

}  // namespace

LogMessage::LogMessage(const char* filename,
                       int line_number,
                       LogSeverity severity)
    : filename_(filename), line_number_(line_number), severity_(severity) {
  stream_ << GetName(severity_) << ":" << filename_ << "(" << line_number_
          << "): ";
}

LogMessage::~LogMessage() {
  printf("%s\n", stream_.str().c_str());
  if (severity_ == FATAL) {
    abort();
  }
}

}  // namespace logging
}  // namespace core