#pragma once

#include <cstdint>
#include <tuple>
#include <vector>

#include "core/logging.h"

constexpr int COLOR_STEPS[10] = {238, 221, 187, 170, 136, 119, 85, 68, 34, 17};

// Copied from: https://belkadan.com/blog/2018/01/Color-Palette-8
std::tuple<int, int, int> colorAtIndex(uint8_t index) {
  if (index == 255) {
    return {0, 0, 0};
  } else if (index < 215) {
    return {
        (255 - (index / 36) * 51),
        (255 - ((index / 6) % 6) * 51),
        (255 - (index % 6) * 51),
    };
  } else {
    int relative_index = index - 215;
    int component_value = COLOR_STEPS[relative_index % 10];
    switch (relative_index / 10) {
      case 0:
        return {component_value, 0, 0};
      case 1:
        return {0, component_value, 0};
      case 2:
        return {0, 0, component_value};
      case 3:
        return {component_value, component_value, component_value};
      default:
        NOTREACHED();
        return {0, 0, 0};
    }
  }
}

// https://en.wikipedia.org/wiki/List_of_software_palettes#Apple_Macintosh_default_16-color_palette
std::tuple<int, int, int> colorAtIndex4Bit(uint8_t nibble) {
  std::vector<std::tuple<int, int, int>> COLORS = {
      {255, 255, 255},  // White
      {255, 255, 0},    // Yellow
      {255, 165, 0},    // Orange
      {255, 0, 0},      // Red
      {255, 0, 255},    // Magenta
      {128, 0, 128},    // Purple
      {0, 0, 255},      // Blue
      {0, 255, 255},    // Cyan
      {0, 255, 0},      // Green
      {0, 100, 0},      // Dark Green
      {128, 64, 0},     // Brown
      {210, 180, 140},  // Tan
      {192, 192, 192},  // Light Grey
      {128, 128, 128},  // Medium Grey
      {64, 64, 64},     // Dark Grey
      {0, 0, 0},        // Black
  };
  CHECK(nibble < 16);
  return COLORS[nibble];
}

std::tuple<int, int, int> colorAtIndex4BitGreyscale(uint8_t nibble) {
  CHECK(nibble < 16);
  int value = 255 - (nibble * 17);
  return {value, value, value};
}
