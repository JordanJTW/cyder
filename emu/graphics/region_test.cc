// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "emu/graphics/region.h"

namespace cyder {
namespace region {

using ::testing::ElementsAre;

TEST(RegionTest, UnionLine) {
  std::vector<int16_t> v1_data = {0, 4, 8, 12, 12, 14, 26, 42};
  std::vector<int16_t> v2_data = {3, 9, 23, 30};

  core::MemoryRegion v1(v1_data.data(), v1_data.size() * 2,
                        /*is_big_endian=*/false);
  core::MemoryRegion v2(v2_data.data(), v2_data.size() * 2,
                        /*is_big_endian=*/false);

  std::vector<int16_t> value = Union(v1, v2);
  EXPECT_THAT(value, ElementsAre(0, 14, 23, 42));
}

TEST(RegionTest, IntersectLine) {
  std::vector<int16_t> v1_data = {0, 4, 8, 12, 12, 14, 26, 42};
  std::vector<int16_t> v2_data = {3, 9, 23, 30};

  core::MemoryRegion v1(v1_data.data(), v1_data.size() * 2,
                        /*is_big_endian=*/false);
  core::MemoryRegion v2(v2_data.data(), v2_data.size() * 2,
                        /*is_big_endian=*/false);

  std::vector<int16_t> value = Intersect(v1, v2);
  EXPECT_THAT(value, ElementsAre(3, 4, 8, 9, 26, 30));
}

TEST(RegionTest, SubtractLine) {
  std::vector<int16_t> v1_data = {0, 4, 8, 12, 12, 14, 26, 42};
  std::vector<int16_t> v2_data = {3, 9, 23, 30};

  core::MemoryRegion v1(v1_data.data(), v1_data.size() * 2,
                        /*is_big_endian=*/false);
  core::MemoryRegion v2(v2_data.data(), v2_data.size() * 2,
                        /*is_big_endian=*/false);

  std::vector<int16_t> value = Subtract(v1, v2);
  EXPECT_THAT(value, ElementsAre(0, 3, 9, 12, 12, 14, 30, 42));
}

TEST(RegionTest, UnionRegion) {
  auto r1 = NewRectRegion(1, 1, 10, 5);
  auto r2 = NewRectRegion(3, 6, 4, 10);

  OwnedRegion output = Union(ConvertRegion(r1), ConvertRegion(r2));

  EXPECT_THAT(output.owned_data, ElementsAre(1, 2, 1, 11,  // y = 1, [1, 11)
                                             6, 2, 3, 7,   // y = 6, [3, 7)
                                             16, 0         // y = 16, END
                                             ));
}

TEST(RegionTest, IntersectRegion) {
  auto r1 = NewRectRegion(1, 1, 10, 5);
  auto r2 = NewRectRegion(3, 3, 4, 10);

  OwnedRegion output = Intersect(ConvertRegion(r1), ConvertRegion(r2));

  EXPECT_THAT(output.owned_data, ElementsAre(3, 2, 3, 7,  // y = 3, [3, 7)
                                             6, 0         // y = 6, END
                                             ));

  OwnedRegion r3;
  r3.rect = {.top = 0, .left = 0, .bottom = 384, .right = 512};
  r3.owned_data = {0, 2, 0, 512, 384, 0};

  OwnedRegion r4;
  r4.rect = {.top = 0, .left = 0, .bottom = 35, .right = 35};
  r4.owned_data = {
      0,  2, 0,  20,          // y = 0, [0, 20)
      15, 2, 0,  35,          // y = 15, [0, 35)
      20, 2, 15, 35,          // y = 20, [15, 35)
      25, 4, 15, 25, 30, 35,  // y = 25, [15, 25), [30, 35)
      30, 2, 15, 35,          // y = 30, [15, 35)
      35, 0                   // y = 35, END
  };

  OwnedRegion output2 = Intersect(ConvertRegion(r3), ConvertRegion(r4));
  EXPECT_THAT(output2.owned_data,
              ElementsAre(0, 2, 0, 20,            // y = 0, [0, 20)
                          15, 2, 0, 35,           // y = 15, [0, 35)
                          20, 2, 15, 35,          // y = 20, [15, 35)
                          25, 4, 15, 25, 30, 35,  // y = 25, [15, 25), [30, 35)
                          30, 2, 15, 35,          // y = 30, [15, 35)
                          35, 0                   // y = 35, END
                          ));
}

TEST(RegionTest, SubtractRegion) {
  auto r1 = NewRectRegion(1, 1, 10, 10);
  auto r2 = NewRectRegion(3, 3, 4, 4);

  OwnedRegion output = Subtract(ConvertRegion(r1), ConvertRegion(r2));

  EXPECT_THAT(output.owned_data,
              ElementsAre(1, 2, 1, 11,        // y = 1, [1, 11)
                          3, 4, 1, 3, 7, 11,  // y = 3, [1, 3), [7, 11)
                          7, 2, 1, 11,        // y = 7, [1, 11)
                          11, 0               // y = 11, END
                          ));
}

TEST(RegionTest, SubtractRegion_Empty) {
  auto r1 = NewRectRegion(40, 40, 100, 80);
  auto r2 = NewRectRegion(0, 20, 512, 364);

  OwnedRegion output = Subtract(ConvertRegion(r1), ConvertRegion(r2));

  EXPECT_THAT(output.owned_data,
              ElementsAre(40, 2, 40, 140,  // y = 40, [40, 140)
                          120, 0           // y = 120, END
                          ));
}

TEST(RegionTest, OffsetRegion) {
  auto r1 = NewRectRegion(1, 1, 10, 10);
  auto output = Offset(ConvertRegion(r1), 3, 6);

  EXPECT_THAT(output.owned_data, ElementsAre(7, 2, 4, 14,  // y = 7 [4, 14)
                                             17, 0         // y = 17, END
                                             ));
}

}  // namespace region
}  // namespace cyder
