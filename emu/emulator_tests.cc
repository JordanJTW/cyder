// Copyright (c) 2025, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include <gmock/gmock-matchers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>

#include "absl/status/status.h"
#include "core/literal_helpers.h"
#include "emu/emulator.h"
#include "emu/memory/memory_map.h"
#include "emu/trap/stack_helpers.h"
#include "emu/trap/trap_dispatcher.h"
#include "emu/trap/trap_manager.h"
#include "third_party/musashi/src/m68k.h"

namespace cyder {
namespace {

using ::testing::_;
using ::testing::Return;

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

  EXPECT_CALL(mock_trap_dispatcher_, Dispatch(Trap::EraseOval))
      .WillOnce(Return(absl::OkStatus()));

  // Calls `EraseOval` (0xA8B9) which is a Toolbox trap.
  CHECK_OK(memory::kSystemMemory.Write<uint16_t>(0x1000, Trap::EraseOval));

  emulator.Run();

  EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x1002);  // Advanced past A-Trap
  EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_SP), memory::kStackStart);
}

TEST_F(EmulatorTests, CallNativeSystemTrap) {
  auto& emulator = Emulator::Instance();
  emulator.Init(0x1000);

  EXPECT_CALL(mock_trap_dispatcher_, Dispatch(Trap::Open))
      .WillOnce(Return(absl::OkStatus()));

  // Calls `Open` (0xA000) which is a System trap.
  CHECK_OK(memory::kSystemMemory.Write<uint16_t>(0x1000, Trap::Open));

  emulator.Run();

  // The routine at `kTrapManagerExitAddress` MUST be executed for system traps.
  EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), memory::kTrapManagerExitAddress);

  emulator.Run();

  EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x1002);  // Advanced past A-Trap
  EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_SP), memory::kStackStart);
}

TEST_F(EmulatorTests, CallFunction) {
  auto& emulator = Emulator::Instance();
  emulator.Init(0xDEADBEEF);

  // Generated with GCC (brew install m68k-elf-binutils):
  //  m68k-elf-as ~/func.s -o output.o
  //  m68k-elf-ld output.o -o output --oformat=binary
  //  build/exe/asm2array output
  constexpr uint8_t kEmulatedFunc[] = {
      0x4E, 0x56, 0xFF, 0xF8,  // link    A6, #-$8
      0x48, 0xE7, 0xC0, 0x00,  // movem.l D0-D1, -(A7)
      0x30, 0x2E, 0x00, 0x0C,  // move.w  ($c,A6), D0
      0x4A, 0x40,              // tst.w   D0
      0x67, 0x00, 0x00, 0x10,  // beq     $20
      0x30, 0x2E, 0x00, 0x0A,  // move.w  ($a,A6), D0
      0x32, 0x2E, 0x00, 0x08,  // move.w  ($8,A6), D1
      0xD0, 0x41,              // add.w   D1, D0
      0x60, 0x00, 0x00, 0x0C,  // bra     $2a
      0x30, 0x2E, 0x00, 0x0A,  // move.w  ($a,A6), D0
      0x32, 0x2E, 0x00, 0x08,  // move.w  ($8,A6), D1
      0x90, 0x81,              // sub.l   D1, D0
      0x3D, 0x40, 0x00, 0x0E,  // move.w  D0, ($e,A6)
      0x4C, 0xDF, 0x00, 0x03,  // movem.l (A7)+, D0-D1
      0x4E, 0x5E,              // unlk    A6
      0x20, 0x5F,              // movea.l (A7)+, A0
      0x5C, 0x4F,              // addq.w  #6, A7
      0x4E, 0xD0,              // jmp     (A0)
  };

  memcpy(memory::kSystemMemory.raw_mutable_ptr() + 0x1000, kEmulatedFunc,
         sizeof(kEmulatedFunc));

  {
    auto result = CallFunction<uint16_t>(0x1000, true, 24_u16, 13_u16);
    EXPECT_EQ(result, 37_u16);
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0xDEADBEEF);
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_SP), memory::kStackStart);
  }
  {
    auto result = CallFunction<uint16_t>(0x1000, false, 24_u16, 13_u16);
    EXPECT_EQ(result, 11_u16);
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0xDEADBEEF);
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_SP), memory::kStackStart);
  }
}

TEST_F(EmulatorTests, CallFunctionNested) {
  auto& emulator = Emulator::Instance();
  emulator.Init(0x1000);

  // Generated with GCC (brew install m68k-elf-binutils):
  //  m68k-elf-as ~/func.s -o output.o
  //  m68k-elf-ld output.o -o output --oformat=binary
  //  build/exe/asm2array output
  //
  // // Returns the upper word if `go_high` is true, or lower word if false.
  // uint16_t GetWord(bool go_high, uint32_t value);
  constexpr uint8_t kGetWordFunc[] = {
      0x4E, 0x56, 0xFF, 0xFC,  // link    A6, #-$4
      0x2D, 0x40, 0xFF, 0xFC,  // move.l  D0, (-$4,A6)
      0x3F, 0x3C, 0x00, 0x00,  // move.w  #$0, -(A7)
      0x2F, 0x2E, 0x00, 0x08,  // move.l  ($8,A6), -(A7)
      0x30, 0x2E, 0x00, 0x0C,  // move.w  ($c,A6), D0
      0x4A, 0x40,              // tst.w   D0
      0x67, 0x00, 0x00, 0x08,  // beq     $20
      0xA8, 0x6A,              // dc.w    $a86a; opcode 1010
      0x60, 0x00, 0x00, 0x04,  // bra     $22
      0xA8, 0x6B,              // dc.w    $a86b; opcode 1010
      0x3D, 0x5F, 0x00, 0x0E,  // move.w  (A7)+, ($e,A6)
      0x20, 0x2E, 0xFF, 0xFC,  // move.l  (-$4,A6), D0
      0x4E, 0x5E,              // unlk    A6
      0x20, 0x5F,              // movea.l (A7)+, A0
      0x5C, 0x4F,              // addq.w  #6, A7
      0x4E, 0xD0,              // jmp     (A0)
  };

  memcpy(memory::kSystemMemory.raw_mutable_ptr() + 0x2000, kGetWordFunc,
         sizeof(kGetWordFunc));

  EXPECT_CALL(mock_trap_dispatcher_, Dispatch(_))
      .WillOnce([](uint16_t trap) {
        EXPECT_EQ(trap, Trap::EraseOval);
        {
          auto result = CallFunction<uint16_t>(0x2000, true, 0xDEAFABBA_u32);
          EXPECT_EQ(result, 0xDEAF);
        }
        {
          auto result = CallFunction<uint16_t>(0x2000, false, 0xDEAFABBA_u32);
          EXPECT_EQ(result, 0xABBA);
        }
        return absl::OkStatus();
      })
      .WillOnce([](uint16_t trap) {
        EXPECT_EQ(trap, Trap::HiWord);

        auto result = CallFunction<uint16_t>(0x2000, false, 0x0CAB0ACE_u32);
        EXPECT_EQ(result, 0xACE);

        uint32_t value = trap::Pop<uint32_t>();
        EXPECT_EQ(value, 0xDEAFABBA);
        return trap::TrapReturn<uint16_t>((value >> 16) & 0xFFFF);
      })
      .WillOnce([](uint16_t trap) {
        EXPECT_EQ(trap, Trap::LoWord);

        uint32_t value = trap::Pop<uint32_t>();
        EXPECT_EQ(value, 0x0CAB0ACE);
        return trap::TrapReturn<uint16_t>(value & 0xFFFF);
      })
      .WillOnce([](uint16_t trap) {
        EXPECT_EQ(trap, Trap::LoWord);

        uint32_t value = trap::Pop<uint32_t>();
        EXPECT_EQ(value, 0xDEAFABBA);
        return trap::TrapReturn<uint16_t>(value & 0xFFFF);
      });

  // Calls `EraseOval` (0xA8B9) which is a Toolbox trap.
  CHECK_OK(memory::kSystemMemory.Write<uint16_t>(0x1000, Trap::EraseOval));
  // TRAP EraseOval
  //   ↳ FUNC GetWord(true, 0xDEAFABBA);
  //     ↳ TRAP HiWord
  //       ↳ FUNC GetWord(false, 0x0CAB0ACE);
  //         ↳ TRAP LoWord
  //   ↳ FUNC GetWord(false, 0xDEAFABBA);
  //     ↳ TRAP LoWord
  emulator.Run();
}

}  // namespace
}  // namespace cyder
