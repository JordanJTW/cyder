#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include "absl/flags/flag.h"
#include "color_palette.h"
#include "core/logging.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "resource_file.h"

ABSL_FLAG(std::vector<std::string>,
          filter_type,
          /*default_value=*/{},
          "The resource group type(s) to filter on");

using namespace rsrcloader;

void ParseIcon(const std::string& name,
               const uint8_t* const data,
               size_t data_size,
               size_t size) {
  std::ofstream icon;
  icon.open(absl::StrCat("/tmp/", name, ".ppm"), std::ios::out);
  auto write_byte = [&](uint8_t byte) {
    for (int i = 7; i >= 0; --i) {
      icon << ((byte & (1 << i)) ? "1 " : "0 ");
    }
  };

  icon << "P1 " << size << " " << size << "\n";
  for (size_t x = 0; x < size / 8; ++x) {
    for (size_t y = 0; y < size; ++y) {
      write_byte(data[y + x * size]);
    }
    icon << "\n";
  }
}

void ParseIcon8bit(const std::string& name,
                   const uint8_t* const data,
                   size_t data_size,
                   size_t size) {
  std::ofstream icon;
  icon.open(absl::StrCat("/tmp/", name, ".ppm"), std::ios::out);
  auto write_byte = [&](uint8_t byte) {
    auto color = colorAtIndex(byte);
    int r = std::get<0>(color);
    int g = std::get<1>(color);
    int b = std::get<2>(color);
    icon << r << " " << g << " " << b << " ";
  };

  icon << "P3 " << size << " " << size << " 255\n";
  for (size_t x = 0; x < size; ++x) {
    for (size_t y = 0; y < size; ++y) {
      write_byte(data[y + x * size]);
    }
  }
}

void ParseIcon4bit(const std::string& name,
                   const uint8_t* const data,
                   size_t data_size,
                   size_t size) {
  std::ofstream icon;
  icon.open(absl::StrCat("/tmp/", name, ".ppm"), std::ios::out);
  auto write_byte = [&](uint8_t byte) {
    auto color = colorAtIndex4Bit(byte);
    int r = std::get<0>(color);
    int g = std::get<1>(color);
    int b = std::get<2>(color);
    icon << r << " " << g << " " << b << " ";
  };

  icon << "P3 " << size << " " << size << " 255\n";
  for (size_t x = 0; x < size / 2; ++x) {
    for (size_t y = 0; y < size; ++y) {
      write_byte((data[y + x * size] & 0xF0) >> 4);
      write_byte(data[y + x * size] & 0x0F);
    }
  }
}

absl::Status Main(const core::Args& args) {
  auto file = TRY(ResourceFile::Load(TRY(args.GetArg(1, "FILENAME"))));

  if (const ResourceGroup* group = file->FindGroupByType('ICON')) {
    for (const auto& resource : group->GetResources()) {
      ParseIcon(absl::StrCat("icon.", resource.GetId()),
                resource.GetData().raw_ptr(), resource.GetSize(), 32);
    }
  }
  if (const ResourceGroup* group = file->FindGroupByType('ICN#')) {
    for (const auto& resource : group->GetResources()) {
      ParseIcon(absl::StrCat("icn#.", resource.GetId()),
                resource.GetData().raw_ptr(), resource.GetSize(), 32);
      ParseIcon(absl::StrCat("icn#.", resource.GetId(), ".mask"),
                resource.GetData().raw_ptr() + 128, resource.GetSize(), 32);
    }
  }
  if (const ResourceGroup* group = file->FindGroupByType('ics#')) {
    for (const auto& resource : group->GetResources()) {
      ParseIcon(absl::StrCat("ics#.", resource.GetId()),
                resource.GetData().raw_ptr(), resource.GetSize(), 16);
    }
  }
  if (const ResourceGroup* group = file->FindGroupByType('icl8')) {
    for (const auto& resource : group->GetResources()) {
      ParseIcon8bit(absl::StrCat("icl8.", resource.GetId()),
                    resource.GetData().raw_ptr(), resource.GetSize(), 32);
    }
  }
  if (const ResourceGroup* group = file->FindGroupByType('ics8')) {
    for (const auto& resource : group->GetResources()) {
      ParseIcon8bit(absl::StrCat("ics8.", resource.GetId()),
                    resource.GetData().raw_ptr(), resource.GetSize(), 16);
    }
  }
  if (const ResourceGroup* group = file->FindGroupByType('icl4')) {
    for (const auto& resource : group->GetResources()) {
      ParseIcon4bit(absl::StrCat("icl4.", resource.GetId()),
                    resource.GetData().raw_ptr(), resource.GetSize(), 32);
    }
  }
  if (const ResourceGroup* group = file->FindGroupByType('ics4')) {
    for (const auto& resource : group->GetResources()) {
      ParseIcon4bit(absl::StrCat("ics4.", resource.GetId()),
                    resource.GetData().raw_ptr(), resource.GetSize(), 16);
    }
  }

  const auto& filter_types = absl::GetFlag(FLAGS_filter_type);
  auto should_allow_type = [filter_types](absl::string_view type) {
    if (filter_types.empty())
      return true;

    return std::find(filter_types.cbegin(), filter_types.cend(), type) !=
           filter_types.cend();
  };

  for (const auto& group : file->groups()) {
    const std::string type_str = GetTypeName(group.GetType());
    if (should_allow_type(type_str)) {
      LOG(INFO) << "Group(type: " << type_str << ", size: " << group.GetSize()
                << "):";
      for (const auto& resource : group.GetResources()) {
        LOG(INFO) << resource << ":\n" << resource.GetData();
      }
    }
  }

  RETURN_IF_ERROR(file->Save("/tmp/test.rsrc"));
  return absl::OkStatus();
}