#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <utility>

#include "core/endian_helpers.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "gen/typegen/typegen_test_types.tdef.h"

class TypegenIntegrationTests : public ::testing::Test {
 protected:
  TypegenIntegrationTests()
      : region_(&internal_buffer_, 64), test_region_(&test_buffer_, 64) {
    memset(internal_buffer_, 0, 64);
    memset(test_buffer_, 0, 64);
  }

  template <typename T>
  void WriteToRegion(T value) {
    auto status = region_.Write<T>(offset_, value);
    CHECK(status.ok()) << std::move(status).message();
    offset_ += sizeof(T);
  }

  template <typename T>
  void CheckWriteType(T value) {
    auto status = WriteType<T>(value, test_region_, /*offset=*/0);
    CHECK(status.ok()) << std::move(status).message();
    ASSERT_THAT(test_buffer_, ::testing::ElementsAreArray(internal_buffer_));
  }

  template <typename T>
  T ReadFromRegion() {
    return MUST(ReadType<T>(region_, /*offset=*/0));
  }

 private:
  uint8_t internal_buffer_[64];
  core::MemoryRegion region_;
  size_t offset_{0};

  uint8_t test_buffer_[64];
  core::MemoryRegion test_region_;
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

  CheckWriteType<Integers>(obj);
}

TEST_F(TypegenIntegrationTests, String) {
  WriteToRegion<std::string>("Hello World!");
  auto obj = ReadFromRegion<String>();
  EXPECT_EQ(obj.string, "Hello World!");
  CheckWriteType<String>(obj);
}

TEST_F(TypegenIntegrationTests, ByteWidth) {
  WriteToRegion<uint32_t>((90210 << 8) | 66);
  WriteToRegion<uint32_t>(407788 | (37 << 24));

  auto obj = ReadFromRegion<ByteWidth>();

  EXPECT_EQ(obj.first, 90210u);
  EXPECT_EQ(obj.byte_one, 66u);
  EXPECT_EQ(obj.byte_two, 37u);
  EXPECT_EQ(obj.last, 407788u);

  CheckWriteType<ByteWidth>(obj);
}

TEST(TypegenReflection, FieldAddition) {
  auto field1 = Field {.offset = 369, .size = 109};
  auto field2 = Field {.offset = 1, .size = 3087};

  auto v1 = field1 + field2;
  EXPECT_EQ(v1.offset, 370);
  EXPECT_EQ(v1.size, 3087);

  auto v2 = field2 + field1;
  EXPECT_EQ(v2.offset, 370);
  EXPECT_EQ(v2.size, 109);
}