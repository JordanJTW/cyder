include(../../gen/typegen/typegen.cmake)

typegen(RESOURCE_TYPES resource_types.tdef)

add_library(RSRC_LIB STATIC resource_file.cc resource_group.cc resource.cc)
target_link_libraries(RSRC_LIB CORE_LIB MACBINARY_LIB RESOURCE_TYPES
  TYPEGEN_PRELUDE)
target_link_libraries(RSRC_LIB absl::statusor)

add_library(RESOURCE_MANAGER STATIC resource_manager.cc)
target_link_libraries(RESOURCE_MANAGER CORE_LIB GENERATED_TYPES RSRC_LIB RESOURCE_TYPES)

add_library(MACBINARY_LIB STATIC macbinary_helpers.cc)
target_link_libraries(MACBINARY_LIB CORE_LIB GRAFPORT_TYPES TYPEGEN_PRELUDE)
