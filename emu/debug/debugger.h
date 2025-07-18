#pragma once

#include <string>
#include <vector>

#include "emu/event_manager.h"

namespace cyder {

class Debugger {
 public:
  static Debugger& Instance();

  bool Prompt();
  void Break();

  void OnTrapEntry(const std::string& trap_name);
  void OnEvent(uint16_t event_type);
  bool OnInstruction();

 private:
  std::vector<std::string> trap_to_break_on;
  bool is_single_step = false;
  bool should_enter_debug = true;
  uint16_t break_event_mask = 0;
  std::string last_command;
};

}  // namespace cyder