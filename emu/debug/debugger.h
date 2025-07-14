#pragma once

#include <string>
#include <vector>

namespace cyder {

class Debugger {
 public:
  static Debugger& Instance();

  bool Prompt();

  void OnTrapEntry(const std::string& trap_name);
  bool OnInstruction();

 private:
  std::vector<std::string> trap_to_break_on;
  bool is_single_step = false;
  bool should_enter_debug = true;
  std::string last_command;
};

}  // namespace cyder