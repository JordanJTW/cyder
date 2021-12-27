#pragma once

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

// Useful helpers for workign with absl::Status/absl::StatusOr.
//
// Inspired by Serenity OS:
//  https://github.com/SerenityOS/serenity/blob/master/AK/Try.h

// Wraps a function which produces an absl::StatusOr<>, returning the value to
// be assigned if Status is OK or providing a guard statement returning errors
// to the parent scope. This uses a GCC extension called Statement Expressions
// supported in GCC/Clang: gcc.gnu.org/onlinedocs/gcc/Statement-Exprs.html
//
// Example:
//  ::absl::Status ParentFunction() {
//    // If an error is returned the function will short-circuit.
//    auto value = TRY(FunctionReturningStatusOr());
//    auto value2 = TRY(AnotherFunctionWithStatusOr());
//    LOG(INFO) << "Value: " << value + value2;
//  }
#define TRY(expr)                             \
  ({                                          \
    auto _status_or = (expr);                 \
    if (ABSL_PREDICT_FALSE(!_status_or.ok())) \
      return std::move(_status_or).status();  \
    std::move(_status_or).value();            \
  })

// If (expr) results in an error (not absl::OkStatus()) then it will return
// from the parent scope. Useful for guard statements.
#define RETURN_IF_ERROR(expr)              \
  do {                                     \
    const absl::Status _status = (expr);   \
    if (ABSL_PREDICT_FALSE(!_status.ok())) \
      return _status;                      \
  } while (0)
