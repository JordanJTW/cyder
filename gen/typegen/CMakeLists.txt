include(../../cmake/gtest.cmake)
include(typegen.cmake)

typegen(BASE_TYPES base_types.tdef)
typegen(GENERATED_TYPES generated_types.tdef)
add_dependencies(GENERATED_TYPES BASE_TYPES)

add_library(TYPEGEN_PRELUDE OBJECT typegen_prelude.cc)

typegen(INCLUDE_TEST include_test.tdef)
typegen(INTEGRATION_TEST_TYPES typegen_test_types.tdef)
target_link_libraries(INTEGRATION_TEST_TYPES INCLUDE_TEST)
gtest(typegen_integration_tests)
target_link_libraries(typegen_integration_tests CORE_LIB INTEGRATION_TEST_TYPES
                      TYPEGEN_PRELUDE INCLUDE_TEST)
