#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <utility>

#include "core/endian_helpers.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "typegen_test_types.h"

class TypegenIntegrationTests : public ::testing::Test {
 protected:
  TypegenIntegrationTests() : region_(&internal_buffer_, 256) {}

  template <typename T>
  void WriteToRegion(T value) {
    auto status = region_.Write<T>(offset_, htobe<T>(value));
    CHECK(status.ok()) << std::move(status).message();
    offset_ += sizeof(T);
  }

  template <typename T>
  T ReadFromRegion() {
    return MUST(ReadType<T>(region_, /*offset=*/0));
  }

 private:
  uint8_t internal_buffer_[256];
  core::MemoryRegion region_;
  size_t offset_{0};
};

// Writes `str` to the memory region as a Pascal string
template <>
void TypegenIntegrationTests::WriteToRegion<std::string>(std::string str) {
  WriteToRegion<uint8_t>(str.size());
  for (auto ch : str) {
    WriteToRegion<uint8_t>(ch);
  }
}

TEST_F(TypegenIntegrationTests, Integers) {
  WriteToRegion<uint8_t>(42);
  WriteToRegion<uint16_t>(1776);
  WriteToRegion<uint32_t>(7880110);
  WriteToRegion<int16_t>(-1992);
  WriteToRegion<int32_t>(-3237331);
  auto obj = ReadFromRegion<Integers>();
  EXPECT_EQ(obj.byte, 42u);
  EXPECT_EQ(obj.uword, 1776u);
  EXPECT_EQ(obj.ulong, 7880110u);
  EXPECT_EQ(obj.sword, -1992);
  EXPECT_EQ(obj.slong, -3237331);
}

TEST_F(TypegenIntegrationTests, String) {
  WriteToRegion<std::string>("Hello World!");
  auto obj = ReadFromRegion<String>();
  EXPECT_EQ(obj.string, "Hello World!");
}

TEST_F(TypegenIntegrationTests, ArrayIntegers) {
  std::vector<uint16_t> values = {1001, 1002, 1004, 1008, 1016, 2048};
  WriteToRegion<uint8_t>(values.size());
  for (auto value : values) {
    WriteToRegion<uint16_t>(value);
  }

  auto obj = ReadFromRegion<ArrayIntegers>();
  EXPECT_EQ(obj.count, values.size());
  ASSERT_THAT(obj.values, ::testing::ElementsAreArray(values));
}

TEST_F(TypegenIntegrationTests, ArrayStructs) {
  // Writes an ArrayIntegers based on |values|:
  auto WriteArrayIntegers = [this](const std::vector<uint16_t> values) {
    WriteToRegion<uint8_t>(values.size());
    for (auto value : values) {
      WriteToRegion<uint16_t>(value);
    }
  };

  // ArrayStructs::count:
  WriteToRegion<uint16_t>(3);
  // ArrayStructs::values[0]:
  WriteArrayIntegers({1, 2, 3});
  // ArrayStructs::values[1]:
  WriteArrayIntegers({10, 20, 30, 40});
  // ArrayStructs::values[2]:
  WriteArrayIntegers({123, 456});

  auto obj = ReadFromRegion<ArrayStructs>();
  EXPECT_EQ(obj.count, 3u);

  EXPECT_EQ(obj.values[0].count, 3u);
  ASSERT_THAT(obj.values[0].values, ::testing::ElementsAre(1, 2, 3));

  EXPECT_EQ(obj.values[1].count, 4u);
  ASSERT_THAT(obj.values[1].values, ::testing::ElementsAre(10, 20, 30, 40));

  EXPECT_EQ(obj.values[2].count, 2u);
  ASSERT_THAT(obj.values[2].values, ::testing::ElementsAre(123, 456));
}

TEST_F(TypegenIntegrationTests, ArrayIntegersNullTerminated) {
  std::vector<uint16_t> values = {1001, 1002, 1004, 1008, 1016, 2048};
  for (auto value : values) {
    WriteToRegion<uint16_t>(value);
  }
  WriteToRegion<uint16_t>(0);

  auto obj = ReadFromRegion<ArrayIntegersNullTerminated>();
  ASSERT_THAT(obj.values, ::testing::ElementsAreArray(values));
}

TEST_F(TypegenIntegrationTests, ArrayStructsNullTerminated) {
  // SimpleStruct#1:
  WriteToRegion<uint16_t>(31);
  WriteToRegion<uint32_t>(41);
  // SimpleStruct#2:
  WriteToRegion<uint16_t>(59);
  WriteToRegion<uint32_t>(26);
  // Null-termination:
  WriteToRegion<uint16_t>(0);

  auto obj = ReadFromRegion<ArrayStructsNullTerminated>();
  EXPECT_EQ(obj.values.size(), 2u);
  EXPECT_EQ(obj.values[0].first, 31u);
  EXPECT_EQ(obj.values[0].last, 41u);
  EXPECT_EQ(obj.values[1].first, 59u);
  EXPECT_EQ(obj.values[1].last, 26u);
}

TEST_F(TypegenIntegrationTests, LessThanOrEqual) {
  std::vector<uint16_t> values = {1001};
  WriteToRegion<uint16_t>(0);
  for (auto value : values) {
    WriteToRegion<uint16_t>(value);
  }

  auto obj = ReadFromRegion<LessThanOrEqual>();
  EXPECT_EQ(obj.count, 0);
  ASSERT_THAT(obj.values, ::testing::ElementsAreArray(values));
}