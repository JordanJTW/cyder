# Copyright (c) 2022, Jordan Werthman
# SPDX-License-Identifier: BSD-2-Clause

#! gtest: define and discover test targets in $target_name
#
# Expects a .cc file with the same name to be present
macro(gtest target_name)
  add_executable(${target_name} ${target_name}.cc)
  target_link_libraries(${target_name} gmock_main)

  include(GoogleTest)
  gtest_discover_tests(${target_name})
endmacro()
