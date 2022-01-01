#include <fstream>
#include <iostream>

#include "color_palette.h"
#include "core/logging.h"

const char* HTML_TEMPLATE_HEADER = R"(
<head>
  <style>
    .container {
      width: 1024px;
      height: 256px;
    }
    .swatch {
      display: inline-block;
      width: 32px;
      height: 32px;
    }
  </style>
</head>
<body>
  <div class="container">
)";

int main() {
  std::ofstream output;
  output.open("/tmp/color_swatch.html", std::ios::out);
  output << HTML_TEMPLATE_HEADER;
  for (int index = 0; index < 256; ++index) {
    auto color = colorAtIndex(index);
    output << "<div class='swatch' style='background-color: rgb("
           << (int)std::get<0>(color) << ", " << (int)std::get<1>(color) << ", "
           << (int)std::get<2>(color) << ");'></div>";
  }
  output << "</div></body>";
  return 0;
}