add_executable(colorswatch colorswatch.cc)
target_link_libraries(colorswatch CORE_LIB)
target_link_libraries(colorswatch absl::str_format)

add_executable(updateinfo updateinfo.cc)
target_link_libraries(updateinfo CORE_LIB)
