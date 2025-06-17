// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "emu/debug/debug_manager.h"

namespace cyder {
namespace {

// TEST(DebugManagerTests, BasicWriteTag) {
//   DebugManager& debug_manager = DebugManager::Instance();
//   debug_manager.Clear();

//   // Write a tag to a memory region.
//   size_t address = 0x1000;
//   size_t size = 0x200;
//   std::string tag = "TestTag";
//   debug_manager.TagMemory(address, size, tag);

//   // Verify that the tag was recorded.
//   auto tags = debug_manager.GetMemoryTags();
//   ASSERT_EQ(tags.size(), 1);
//   EXPECT_EQ(tags[0].start, address);
//   EXPECT_EQ(tags[0].end, address + size);
//   EXPECT_EQ(tags[0].tag, tag);
// }

// TEST(DebugManagerTests, WriteToExistingTag) {
//   DebugManager& debug_manager = DebugManager::Instance();
//   debug_manager.Clear();

//   debug_manager.TagMemory(0x1000, 0x200, "TestTag");
//   debug_manager.RecordWrite(0x1020, 0x10);

//   // Verify that the write was recorded in the existing tag.
//   auto tags = debug_manager.GetMemoryTags();
//   ASSERT_EQ(tags.size(), 1);
//   EXPECT_EQ(tags[0].start, 0x1000);
//   EXPECT_EQ(tags[0].end, 0x1200);
//   EXPECT_EQ(tags[0].tag, "TestTag");
// }

// TEST(DebugManagerTests, TagExisitingWrite) {
//   DebugManager& debug_manager = DebugManager::Instance();
//   debug_manager.Clear();

//   debug_manager.RecordWrite(0x0, 0x10000);
//   debug_manager.TagMemory(0x1000, 0x200, "TestTag");

//   // Verify that the write was recorded in the existing tag.
//   auto tags = debug_manager.GetMemoryTags();
//   ASSERT_EQ(tags.size(), 2);
//   EXPECT_EQ(tags[1].start, 0x0);
//   EXPECT_EQ(tags[1].end, 0x10000);
//   EXPECT_EQ(tags[1].tag, "");
//   EXPECT_EQ(tags[0].start, 0x1000);
//   EXPECT_EQ(tags[0].end, 0x1200);
//   EXPECT_EQ(tags[0].tag, "TestTag");
// }

// TEST(DebugManagerTests, GroupOverlappingWrites) {
//   DebugManager& debug_manager = DebugManager::Instance();
//   debug_manager.Clear();

//   debug_manager.RecordWrite(0x0, 0x10);
//   debug_manager.RecordWrite(0x14, 0x20);
//   debug_manager.RecordWrite(0x08, 0x16);
//   debug_manager.RecordWrite(0x100, 0x116);

//   EXPECT_THAT(debug_manager.GetMemoryTags(),
//               ::testing::ElementsAre(MemorySpan{0x0, 0x20, ""},
//                                      MemorySpan{0x100, 0x116, ""}));

//   debug_manager.RecordWrite(0x50, 0x92);
//   debug_manager.RecordWrite(0x20, 0x40);
//   debug_manager.RecordWrite(0x30, 0x60);

//   EXPECT_THAT(debug_manager.GetMemoryTags(),
//               ::testing::ElementsAre(MemorySpan{0x0, 0x92, ""},
//                                      MemorySpan{0x100, 0x116, ""}));

//   debug_manager.RecordWrite(0x45, 0x90);
//   debug_manager.RecordWrite(0x90, 0x100);

//   for (const auto& tag : debug_manager.GetMemoryTags()) {
//     std::cout << tag << std::endl;
//   }

//   EXPECT_THAT(debug_manager.GetMemoryTags(),
//               ::testing::ElementsAre(MemorySpan{0x0, 0x116, ""}));
// }

void Tag(size_t address, size_t size, std::string tag) {
  DebugManager::Instance().TagMemory(address, address + size, tag);
}

// TEST(DebugManagerTests, OrderPrint) {
//   DebugManager& debug_manager = DebugManager::Instance();
//   debug_manager.Clear();

//   Tag(0xc942, 0x6c, "GrafPort");
//   Tag(0xc944, 0xe, "BitMap");
//   Tag(0xc94a, 0x8, "Rect");
//   Tag(0xc952, 0x8, "Rect");
//   Tag(0xc962, 0x8, "Pattern");
//   Tag(0xc96a, 0x8, "Pattern");
//   Tag(0xc972, 0x4, "Point");
//   Tag(0xc976, 0x4, "Point");
//   Tag(0xc97c, 0x8, "Pattern");
//   Tag(0xca47, 0x9c, "WindowRecord");
//   Tag(0xca47, 0x6c, "GrafPort");
//   Tag(0xca49, 0xe, "BitMap");
//   Tag(0xca4f, 0x8, "Rect");
//   Tag(0xca57, 0x8, "Rect");
//   Tag(0xca67, 0x8, "Pattern");
//   Tag(0xca6f, 0x8, "Pattern");
//   Tag(0xca77, 0x4, "Point");
//   Tag(0xca7b, 0x4, "Point");
//   Tag(0xca81, 0x8, "Pattern");
//   Tag(0xcae3, 0xa, "Region");
//   Tag(0xcae5, 0x8, "Rect");
//   Tag(0xcaf4, 0xa, "Region");
//   Tag(0xcaf6, 0x8, "Rect");
//   Tag(0xcafe, 0xa, "Region");
//   Tag(0xcb00, 0x8, "Rect");
//   Tag(0xcb08, 0xa, "Region");
//   Tag(0xcb0a, 0x8, "Rect");
//   Tag(0x7848c, 0x8, "Rect");
//   Tag(0x78494, 0x8, "Rect");
//   Tag(0x78524, 0x82, "QDGlobals");
//   Tag(0x78528, 0xe, "BitMap");
//   Tag(0x7852e, 0x8, "Rect");
//   Tag(0x78536, 0x44, "Cursor");
//   Tag(0x7857a, 0x8, "Pattern");
//   Tag(0x78582, 0x8, "Pattern");
//   Tag(0x7858a, 0x8, "Pattern");
//   Tag(0x78592, 0x8, "Pattern");
//   Tag(0x7859a, 0x8, "Pattern");

//   debug_manager.PrintMemoryMap();
// }

TEST(DebugManagerTests, OrderPrint2) {
  DebugManager& debug_manager = DebugManager::Instance();
  debug_manager.Clear();

  DebugManager::Instance().RecordWrite(0xaec, 0xaf0);
  DebugManager::Instance().RecordWrite(0x2c00, 0x2c08);
  DebugManager::Instance().RecordWrite(0x3c00, 0x3efc);
  DebugManager::Instance().TagMemory(0x3c04, 0x3efc, "CODE1");
  DebugManager::Instance().RecordWrite(0x78208, 0x78214);
  DebugManager::Instance().RecordWrite(0x78220, 0x78230);
  DebugManager::Instance().TagMemory(0x78220, 0x78228, "SegmentTableEntry");
  DebugManager::Instance().RecordWrite(0x130, 0x134);
  DebugManager::Instance().RecordWrite(0x910, 0x911);
  DebugManager::Instance().RecordWrite(0x2f0, 0x2f4);
  DebugManager::Instance().RecordWrite(0x100, 0x102);
  DebugManager::Instance().RecordWrite(0x28e, 0x290);
  DebugManager::Instance().RecordWrite(0xa02, 0xa0a);
  DebugManager::Instance().RecordWrite(0x31a, 0x31e);
  DebugManager::Instance().RecordWrite(0x28, 0x2c);
  DebugManager::Instance().RecordWrite(0x7f5fa, 0x80000);
  DebugManager::Instance().RecordWrite(0x904, 0x90c);
  DebugManager::Instance().RecordWrite(0x77ffc, 0x78000);
  DebugManager::Instance().RecordWrite(0x106, 0x108);

  debug_manager.PrintMemoryMap();
}

}  // namespace
}  // namespace cyder
