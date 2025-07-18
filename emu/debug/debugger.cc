#include "emu/debug/debugger.h"

#include <algorithm>
#include <iostream>
#include <regex>

#include "absl/base/no_destructor.h"
#include "core/memory_reader.h"
#include "core/status_helpers.h"
#include "emu/debug/debug_manager.h"
#include "emu/event_manager.tdef.h"
#include "emu/graphics/grafport_types.tdef.h"
#include "emu/memory/memory_map.h"
#include "emu/window_manager.tdef.h"
#include "third_party/musashi/src/m68k.h"

namespace cyder {
namespace {

bool ReadTypePrompt(const std::string& line) {
  std::smatch match;
  if (!std::regex_match(line, match, std::regex("type (\\w+) (0x[\\d|\\w]+)")))
    return false;

  int address = std::stoi(match[2], nullptr, 16);
  if (match[1] == "rect") {
    auto rect = MUST(ReadType<Rect>(cyder::memory::kSystemMemory, address));
    std::cout << rect << "\n";
  } else if (match[1] == "point") {
    auto point = MUST(ReadType<Point>(cyder::memory::kSystemMemory, address));
    std::cout << point << "\n";
  } else if (match[1] == "event") {
    auto event =
        MUST(ReadType<EventRecord>(cyder::memory::kSystemMemory, address));
    std::cout << event << "\n";
  } else if (match[1] == "window") {
    auto window =
        MUST(ReadType<WindowRecord>(cyder::memory::kSystemMemory, address));
    std::cout << window << "\n";
  } else {
    std::cout << "Unknown type: " << match[1] << "\n";
  }
  return true;
}

bool ReadBreakTrapPrompt(const std::string& line,
                         std::vector<std::string>& trap_to_break_on) {
  std::smatch match;
  if (!std::regex_match(line, match, std::regex("break trap (\\w+)")))
    return false;

  trap_to_break_on.push_back(match[1]);
  return true;
}

bool ReadBreakEventPrompt(const std::string& line, uint16_t& break_event_mask) {
  std::smatch match;
  if (!std::regex_match(line, match, std::regex("break event (\\w+)")))
    return false;

  if (match[1] == "null") {
    break_event_mask |= (1 << kNullEvent);
  } else if (match[1] == "mouseDown") {
    break_event_mask |= (1 << kMouseDown);
  } else if (match[1] == "mouseUp") {
    break_event_mask |= (1 << kMouseUp);
  } else if (match[1] == "keyDown") {
    break_event_mask |= (1 << kKeyDown);
  } else if (match[1] == "KeyUp") {
    break_event_mask |= (1 << kKeyUp);
  } else if (match[1] == "windowUpdate") {
    break_event_mask |= (1 << kWindowUpdate);
  } else if (match[1] == "windowActivate") {
    break_event_mask |= (1 << kWindowActivate);
  } else {
    return false;
  }
  return true;
}

}  // namespace

// static
Debugger& Debugger::Instance() {
  static absl::NoDestructor<Debugger> s_instance;
  return *s_instance;
}

void Debugger::Break() {
  should_enter_debug = true;
  m68k_end_timeslice();
}

void Debugger::OnTrapEntry(const std::string& trap_name) {
  if (std::find(trap_to_break_on.cbegin(), trap_to_break_on.cend(),
                trap_name) != trap_to_break_on.cend()) {
    Break();
  }
}

void Debugger::OnEvent(uint16_t event_type) {
  if (break_event_mask & (1 << event_type)) {
    Break();
  }
}

bool Debugger::OnInstruction() {
  bool should_disassemble = is_single_step;
  if (is_single_step) {
    m68k_end_timeslice();
    is_single_step = false;
  }
  return should_disassemble;
}

bool Debugger::Prompt() {
  if (!should_enter_debug)
    return true;

  std::cout << "? ";
  std::string line;
  std::getline(std::cin, line);

  if (line == "")
    line = last_command;

  last_command = line;

  if (line == "run") {
    should_enter_debug = false;
    return true;
  }

  if (line == "mem") {
    cyder::DebugManager::Instance().PrintMemoryMap();
    return false;
  }

  if (line == "stack") {
    uint32_t stack_ptr = m68k_get_reg(NULL, M68K_REG_SP);
    std::cout << "\n"
              << cyder::memory::kSystemMemory.Create(
                     "stack", stack_ptr, cyder::memory::kStackStart - stack_ptr)
              << std::endl;
    return false;
  }

  if (line == "step") {
    is_single_step = true;
    return true;
  }

  if (line == "quit" || line == "exit") {
    exit(0);
  }

  if (ReadBreakTrapPrompt(line, trap_to_break_on))
    return false;

  if (ReadBreakEventPrompt(line, break_event_mask))
    return false;

  if (ReadTypePrompt(line))
    return false;

  std::cerr << "Unknown command: '" << line << "'" << std::endl;
  return false;
}

}  // namespace cyder