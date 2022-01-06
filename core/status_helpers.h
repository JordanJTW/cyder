#pragma once

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "logging.h"
#include "macro_helpers.h"

// Useful helpers for working with absl::Status/absl::StatusOr.
//
// Inspired by Serenity OS:
//  https://github.com/SerenityOS/serenity/blob/master/AK/Try.h

// Wraps a function which produces an absl::StatusOr<>, returning the value to
// be assigned if Status is OK or providing a guard statement returning errors
// to the parent scope. This uses a GCC extension called Statement Expressions
// supported in GCC/Clang: gcc.gnu.org/onlinedocs/gcc/Statement-Exprs.html
//
// Optionally an error message can be passed as a secondary argument to log
// whenever an error status is returned for a given TRY statement.
//
// Example:
//  ::absl::Status ParentFunction() {
//    // If an error is returned the function will short-circuit.
//    auto value = TRY(FunctionReturningStatusOr());
//    auto value2 = TRY(AnotherFunctionWithStatusOr(), "Error message");
//    LOG(INFO) << "Value: " << value + value2;
//  }
#define TRY(...) MACRO_HELPERS_OVERLOAD_MACRO(_TRY, __VA_ARGS__)

#define _TRY1(expr)                             \
  ({                                            \
    auto _status_or = (expr);                   \
    if (ABSL_PREDICT_FALSE(!_status_or.ok())) { \
      return std::move(_status_or).status();    \
    }                                           \
    std::move(_status_or).value();              \
  })

#define _TRY2(expr, error_message)              \
  ({                                            \
    auto _status_or = (expr);                   \
    if (ABSL_PREDICT_FALSE(!_status_or.ok())) { \
      LOG(ERROR) << (error_message);            \
      return std::move(_status_or).status();    \
    }                                           \
    std::move(_status_or).value();              \
  })

// MUST() works similarly to TRY() but CHECK() fails if the returned Status
// is not OK. This will cause crashes so care should be taken when using it.
#define MUST(expr)                                            \
  ({                                                          \
    auto _status_or = (expr);                                 \
    CHECK(_status_or.ok()) << std::move(_status_or).status(); \
    std::move(_status_or).value();                            \
  })

// If (expr) results in an error (not absl::OkStatus()) then it will return
// from the parent scope. Useful for guard statements.
#define RETURN_IF_ERROR(expr)              \
  do {                                     \
    const absl::Status _status = (expr);   \
    if (ABSL_PREDICT_FALSE(!_status.ok())) \
      return _status;                      \
  } while (0)
