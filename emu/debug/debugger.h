#pragma once

class Debugger {
 public:
  static Debugger& Instance();

  bool Prompt();

  bool ShouldBreakOnExitTrap() { return should_break_on_exit; }
  bool ShouldBreakOnSystemTaskTrap() { return should_break_on_sys; }

 private:
  bool should_break_on_exit = false;
  bool should_break_on_sys = false;
};