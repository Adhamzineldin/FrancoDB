# =========================================================
# FrancoDB Test Suite - CMake Configuration
# Build: cmake --build . --target run_all_tests
# Run:   ctest -R ComprehensiveTestSuite --output-on-failure
# =========================================================

enable_testing()

include_directories(${CMAKE_CURRENT_LIST_DIR}/framework)

# =========================================================
# COMPREHENSIVE TEST SUITE - Single Executable
# =========================================================

set(TEST_SUITE_SOURCES
    run_all_tests.cpp
        execution/column_tests.cpp
        execution/join_tests.cpp
        execution/foreign_key_tests.cpp
        execution/groupby_tests.cpp
        execution/orderby_tests.cpp
        execution/limit_distinct_tests.cpp
    modules/module_tests_stub.cpp
)

add_executable(run_all_tests ${TEST_SUITE_SOURCES})
target_link_libraries(run_all_tests PRIVATE francodb_lib)
target_include_directories(run_all_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/include ${CMAKE_CURRENT_LIST_DIR}/framework)

# Register with CTest
add_test(NAME ComprehensiveTestSuite COMMAND run_all_tests)
set_tests_properties(ComprehensiveTestSuite PROPERTIES
    TIMEOUT 60
    LABELS "comprehensive;sql;s_plus"
)

# =========================================================
# INDIVIDUAL MODULE TESTS (Auto-discovered)
# =========================================================

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

# Exclude comprehensive test suite files
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*(run_all_tests|column_tests|join_tests|foreign_key_tests|groupby_tests|orderby_tests|limit_distinct_tests|module_tests_stub)\\.cpp$")

foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE francodb_lib)
    target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/src/include ${CMAKE_CURRENT_LIST_DIR}/framework)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# =========================================================
# CUSTOM TARGETS
# =========================================================

add_custom_target(test_quick
    COMMAND ${CMAKE_CTEST_COMMAND} -R ComprehensiveTestSuite --output-on-failure
    DEPENDS run_all_tests
    COMMENT "Running comprehensive test suite..."
)

add_custom_target(test_all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running all tests..."
)

add_custom_target(test_verbose
    COMMAND ${CMAKE_CTEST_COMMAND} -R ComprehensiveTestSuite -V
    DEPENDS run_all_tests
    COMMENT "Running tests with verbose output..."
)
