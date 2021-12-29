#pragma once

// Counts the number of arguments to a variadic macro (up to 9).
//
// All of the arguments are passed to _COUNT_ARGS and expanded so that
// a call with two arguments would look like:
//
//            _COUNT_ARGS(A, B, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
//                                                   |
//    __ARG_PATTERN_MATCH(A, B, 9, 8, 7, 6, 5, 4, 3, N, ...)
//
// The __VA_ARGS__ shifts the list over N places making the correct
// argument count line up with N in _ARG_PATTERN_MATCH.
#define MACRO_HELPERS_COUNT_ARGS(...) \
  __ARG_PATTERN_MATCH(__VA_ARGS__, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define __ARG_PATTERN_MATCH(_1, _2, _3, _4, _5, _6, _7, _8, _9, N, ...) N

// Allows a macro NAME to be overloaded with different argument counts.
// Usage:
//    #define FOO2(arg1, arg2)  <do "two stuff">
//    #define FOO3(arg1, arg2, arg3)  <do "three stuff">
//    #define FOO(...) MACRO_HELPERS_OVERLOAD_MACRO(FOO, __VA_ARGS__)
//
// The variadic arguments are counted using MACRO_HELPERS_COUNT_ARGS
// and the resulting argument count is then appended to NAME to
// produce the macro to call.
#define MACRO_HELPERS_OVERLOAD_MACRO(NAME, ...) \
  __OVERLOADED_MACRO(NAME, MACRO_HELPERS_COUNT_ARGS(__VA_ARGS__))(__VA_ARGS__)
#define __OVERLOADED_MACRO(macroName, number_of_args) \
  __OVERLOADED_MACRO_EXPAND(macroName, number_of_args)
#define __OVERLOADED_MACRO_EXPAND(macroName, number_of_args) \
  macroName##number_of_args
