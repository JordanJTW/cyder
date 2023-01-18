include(../../gen/typegen/typegen.cmake)

typegen(RESOURCE_TYPES resource_types.tdef)

add_library(RSRC_LIB resource_file.cc resource_group.cc resource.cc)
target_link_libraries(RSRC_LIB CORE_LIB RESOURCE_TYPES TYPEGEN_PRELUDE)
target_link_libraries(RSRC_LIB absl::statusor)

add_library(RESOURCE_MANAGER resource_manager.cc)
target_link_libraries(RESOURCE_MANAGER CORE_LIB RSRC_LIB RESOURCE_TYPES)

add_library(MACBINARY_LIB macbinary_helpers.cc)
target_link_libraries(MACBINARY_LIB CORE_LIB GRAFPORT_TYPES TYPEGEN_PRELUDE)

