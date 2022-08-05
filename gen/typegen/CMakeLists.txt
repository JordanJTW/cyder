include(../../cmake/gtest.cmake)

find_package (Python3 3.8 COMPONENTS Interpreter)

file(GLOB TYPEGEN_SRC *.py)

#! typegen: generates types based on a defintion file
macro(typegen target_name type_definition)
    get_filename_component(output_name ${type_definition} NAME_WE)

    add_custom_command(
        OUTPUT ${output_name}.cc ${output_name}.h
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/main.py
            ${CMAKE_CURRENT_SOURCE_DIR}/${type_definition}
            ${CMAKE_CURRENT_BINARY_DIR}/${output_name}
        DEPENDS ${TYPEGEN_SRC} ${type_definition}
    )

    add_library(${target_name} OBJECT ${output_name}.cc polyfill.cc)
    target_link_libraries(${target_name} CORE_LIB absl::statusor)
    target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
endmacro()

typegen(GENERATED_TYPES generated_types.tdef)

typegen(INTEGRATION_TEST_TYPES typegen_test_types.tdef)
gtest(typegen_integration_tests)
target_link_libraries(typegen_integration_tests CORE_LIB INTEGRATION_TEST_TYPES)
