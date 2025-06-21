#include "emu/debug/debugger.h"

#include <iostream>
#include <regex>

#include "absl/base/no_destructor.h"
#include "core/memory_reader.h"
#include "core/status_helpers.h"
#include "emu/debug/debug_manager.h"
#include "emu/graphics/grafport_types.tdef.h"
#include "emu/memory/memory_map.h"

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
  } else {
    std::cout << "Unknown type: " << match[1] << "\n";
  }
  return true;
}

// static
Debugger& Debugger::Instance() {
  static absl::NoDestructor<Debugger> s_instance;
  return *s_instance;
}

bool Debugger::Prompt() {
  std::cout << "? ";
  std::string line;
  std::getline(std::cin, line);

  if (line == "")
    line = last_command;

  last_command = line;

  if (line == "run")
    return true;

  if (line == "mem") {
    cyder::DebugManager::Instance().PrintMemoryMap();
    return false;
  }

  if (line == "trap exit") {
    should_break_on_exit = true;
    return false;
  }

  if (line == "trap sys") {
    should_break_on_sys = true;
    return false;
  }

  if (line == "step") {
    is_single_step = true;
    return true;
  }

  if (ReadTypePrompt(line))
    return false;

  std::cerr << "Unknown command: '" << line << "'" << std::endl;
  return false;
}