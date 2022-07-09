#pragma once

#include <cstdint>

namespace rsrcloader {

// The in memory representation of resource fork structures from:
// https://mirror.informatimago.com/next/developer.apple.com/documentation/mac/MoreToolbox/MoreToolbox-99.html

/* Resource header
    4 bytes - Offset from beginning of resource fork to resource data
    4 bytes - Offset from beginning of resource fork to resource map
    4 bytes - Length of resource data
    4 bytes - Length of resource map
*/
struct InMemoryHeader {
  uint32_t data_offset;
  uint32_t map_offset;
  uint32_t data_length;
  uint32_t map_length;
} __attribute__((packed));
;

/* Resource Map
    16 bytes - Reserved for a copy of resource header
    4 bytes  - Reserved for handle to next resource map
    2 bytes  - Reserved for file reference number
    2 bytes  - Resource fork attributes
    2 bytes  - Offset from beginning of map to resource type list
    2 bytes  - Offset from beginning of map to resource name list
    2 bytes  - Number of types in the map minus 1
*/
struct InMemoryMapHeader {
  InMemoryHeader header;
  uint32_t next_map_handle;
  uint16_t file_ref_number;
  uint16_t file_attributes;
  uint16_t type_list_offset;
  uint16_t name_list_offset;
  uint16_t type_list_count;
} __attribute__((packed));

/* Item in resource type list
    4 bytes - Resource type
    2 bytes - Number of resources of this type in map minus 1
    2 bytes - Offset from beginning of resource type list to
                reference list for this type
*/
struct InMemoryTypeItem {
  uint32_t type;
  uint16_t count;
  uint16_t offset;
};

/* Entry in the reference list for a resource type
    2 bytes - Resource ID
    2 bytes - Offset from beginning of resource name list to
                resource name
    1 byte  - Resource attributes
    3 bytes - Offset from beginning of resource data to data
                for this resource
    4 bytes - Reserved for handle to resource
*/
struct InMemoryReferenceEntry {
  uint16_t id;
  uint16_t name_offset;
  uint32_t offset;
  uint32_t handle;
};

}  // namespace rsrcloader