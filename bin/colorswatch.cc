#include <fstream>
#include <iostream>

#include "absl/strings/str_format.h"
#include "color_palette.h"

constexpr char HTML_TEMPLATE_HEADER[] = R"(
<head>
  <style>
    .header {
      padding: 5px;
      font-size: 30px;
      font-weight: bold;
    }
    .swatch {
      display: inline-block;
      width: 32px;
      height: 32px;
      outline: 1px solid;
    }
  </style>
</head>
<body>
)";

constexpr char HTML_TEMPLATE_SECTION_HEADER[] = R"(
  <div class="header">%s</div>
  <div>
)";

constexpr char HTML_TEMPLATE_SWATCH[] = R"(
    <div class="swatch", style="background-color: rgb(%d, %d, %d);"></div>
)";

constexpr char HTML_TEMPLATE_SECTION_FOOTER[] = "</div>";
constexpr char HTML_TEMPLATE_FOOTER[] = "</body>";

void GenerateSection(const std::string& name,
                     std::tuple<int, int, int> (*colorAtIndex)(uint8_t),
                     size_t count,
                     std::ostream& output) {
  output << absl::StrFormat(HTML_TEMPLATE_SECTION_HEADER, name);
  for (int index = 0; index < count; ++index) {
    auto color = colorAtIndex(index);
    output << absl::StrFormat(HTML_TEMPLATE_SWATCH, std::get<0>(color),
                              std::get<1>(color), std::get<2>(color));
  }
  output << HTML_TEMPLATE_SECTION_FOOTER;
}

int main() {
  std::ofstream output;
  output.open("/tmp/color_swatch.html", std::ios::out);
  output << HTML_TEMPLATE_HEADER;

  GenerateSection("4-Bit Greyscale", colorAtIndex4BitGreyscale, 16, output);
  GenerateSection("4-Bit Color", colorAtIndex4Bit, 16, output);
  GenerateSection("8-Bit Color", colorAtIndex, 256, output);

  output << HTML_TEMPLATE_FOOTER;
  return 0;
}