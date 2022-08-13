include(../../cmake/gtest.cmake)
include(typegen.cmake)

typegen(GENERATED_TYPES generated_types.tdef)

add_library(TYPEGEN_PRELUDE OBJECT typegen_prelude.cc)

typegen(INTEGRATION_TEST_TYPES typegen_test_types.tdef)
gtest(typegen_integration_tests)
target_link_libraries(typegen_integration_tests CORE_LIB INTEGRATION_TEST_TYPES TYPEGEN_PRELUDE)
