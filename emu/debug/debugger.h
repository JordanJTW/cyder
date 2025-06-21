#pragma once

#include <string>

class Debugger {
 public:
  static Debugger& Instance();

  bool Prompt();

  bool ShouldBreakOnExitTrap() { return should_break_on_exit; }
  bool ShouldBreakOnSystemTaskTrap() { return should_break_on_sys; }
  bool IsSingleStep() {
    bool current = is_single_step;
    is_single_step = false;
    return current;
  }

 private:
  bool should_break_on_exit = false;
  bool should_break_on_sys = false;
  bool is_single_step = false;
  std::string last_command;
};