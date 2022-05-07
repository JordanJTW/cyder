#include <sys/stat.h>
#include <cstring>
#include <iostream>

#include "core/endian_helpers.h"
#include "core/logging.h"

size_t GetFileSize(const char* path) {
  struct stat stat_buf;
  int rc = stat(path, &stat_buf);
  return rc == 0 ? stat_buf.st_size : -1;
}

struct Metadata {
  uint8_t name_len;
  char name[63];
  char type[4];
  char author[4];
  uint16_t flags;
  char reserved[8];
  uint32_t datal;   
  uint32_t rsrcl;
  uint32_t time_created;
  uint32_t time_modified;
} __attribute__((packed));

int main(int argc, char** argv) {
  const char* inpath = argv[1];
  size_t size = GetFileSize(inpath);
  CHECK_EQ(size, 128u);

  uint8_t data[size];
  FILE* file = fopen(inpath, "rb");
  fread(data, sizeof(uint8_t), size, file);
  fclose(file);

  Metadata metadata;
  memcpy(&metadata, data + 1, sizeof(Metadata));

  LOG(INFO) << std::string(metadata.name, metadata.name_len) << "(type:"
            << std::string(metadata.type, 4) << ",author:"
            << std::string(metadata.author, 4) << "):"
            << " flags: " << be16toh(metadata.flags)
            << " data len: " << be32toh(metadata.datal)
            << " rsrc len: " << be32toh(metadata.rsrcl)
            << " created: " << be32toh(metadata.time_created)
            << " modified: " << be32toh(metadata.time_modified);

  metadata.rsrcl = htobe32(12524);

  std::string output = "/tmp/output.info";
  file = fopen(output.c_str(), "wb");
  uint8_t blank = 0;
  fwrite(&blank, sizeof(uint8_t), 1, file);
  fwrite(&metadata, sizeof(Metadata), 1, file);
  for (int i=0; i < 29; ++i) {
    fwrite(&blank, sizeof(uint8_t), 1, file);
  }
  fclose(file);
  return 0;
}
