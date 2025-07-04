// Copyright (c) 2025, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include <gmock/gmock-matchers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>

#include "absl/status/status.h"
#include "emu/emulator.h"
#include "emu/memory/memory_map.h"
#include "emu/trap/stack_helpers.h"
#include "emu/trap/trap_dispatcher.h"
#include "emu/trap/trap_manager.h"
#include "third_party/musashi/src/m68k.h"

namespace cyder {
namespace {

class MockTrapDispatcher : public trap::TrapDispatcher {
 public:
  ~MockTrapDispatcher() override = default;

  MOCK_METHOD(absl::Status, Dispatch, (uint16_t trap), (override));
};

class MockSegmentLoader : public SegmentLoader {
 public:
  MOCK_METHOD(absl::StatusOr<Ptr>, Load, (uint16_t segment_id), (override));
};

class EmulatorTests : public ::testing::Test {
 protected:
  constexpr static unsigned int kInitialPC = 0x1000;

  EmulatorTests() : trap_manager(mock_segment_loader_, mock_trap_dispatcher_) {}

  void SetUp() override { Emulator::Instance().Init(kInitialPC); }

  MockSegmentLoader mock_segment_loader_;
  MockTrapDispatcher mock_trap_dispatcher_;
  trap::TrapManager trap_manager;
};

TEST_F(EmulatorTests, CallNativeToolboxTrap) {
  auto& emulator = Emulator::Instance();
  emulator.Init(0x1000);

  EXPECT_CALL(mock_trap_dispatcher_, Dispatch(0xA8B9)).WillOnce([]() {
    return absl::OkStatus();
  });

  // Calls `EraseOval` (0xA8B9) which is a Toolbox trap.
  CHECK_OK(memory::kSystemMemory.Write<uint16_t>(0x1000, 0xA8B9));

  emulator.Run();

  EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x1002);  // Advanced past A-Trap
  EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_SP), memory::kStackStart);
}

TEST_F(EmulatorTests, CallNativeSystemTrap) {
  auto& emulator = Emulator::Instance();
  emulator.Init(0x1000);

  EXPECT_CALL(mock_trap_dispatcher_, Dispatch(0xA000)).WillOnce([]() {
    return absl::OkStatus();
  });

  // Calls `Open` (0xA000) which is a System trap.
  CHECK_OK(memory::kSystemMemory.Write<uint16_t>(0x1000, 0xA000));

  emulator.Run();

  // The routine at `kTrapManagerExitAddress` MUST be executed for system traps.
  EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), memory::kTrapManagerExitAddress);

  emulator.Run();

  EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x1002);  // Advanced past A-Trap
  EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_SP), memory::kStackStart);
}

}  // namespace
}  // namespace cyder
