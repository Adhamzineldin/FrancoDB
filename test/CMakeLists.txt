# =========================================================
# FrancoDB Test Suite - CMake Configuration
# Build: cmake --build . --target run_all_tests
# Run:   ctest -R ComprehensiveTestSuite --output-on-failure
# =========================================================

cmake_minimum_required(VERSION 3.10)
enable_testing()

include_directories(${CMAKE_CURRENT_LIST_DIR}/framework)

# =========================================================
# 1. DISCOVER SOURCES
# =========================================================

# Collect all CPP files relative to the test directory
file(GLOB_RECURSE ALL_CPP_FILES RELATIVE ${CMAKE_CURRENT_LIST_DIR} "*.cpp")

# Filter out ONLY the runner - all other tests will be framework-integrated
set(TEST_MODULES ${ALL_CPP_FILES})
list(FILTER TEST_MODULES EXCLUDE REGEX "run_all_tests\\.cpp$")

# Define the runner source explicitly
set(RUNNER_SOURCE "run_all_tests.cpp")

# DEBUG: Print discovered files to help verify what CMake sees
message(STATUS "--- FrancoDB Test Discovery ---")
message(STATUS "Main Runner: ${RUNNER_SOURCE}")
message(STATUS "Test Modules Found: ${TEST_MODULES}")

# =========================================================
# 2. COMPREHENSIVE TEST SUITE (Single Executable)
# =========================================================

# Check if we actually found tests to avoid empty executable errors
list(LENGTH TEST_MODULES LIST_LEN)
if(LIST_LEN GREATER 0)
    add_executable(run_all_tests ${RUNNER_SOURCE} ${TEST_MODULES})
    target_link_libraries(run_all_tests PRIVATE francodb_lib)
    if(WIN32)
        target_link_libraries(run_all_tests PRIVATE ws2_32)
    endif()
    target_include_directories(run_all_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/include ${CMAKE_CURRENT_LIST_DIR}/framework)

    add_test(NAME ComprehensiveTestSuite COMMAND run_all_tests)
    set_tests_properties(ComprehensiveTestSuite PROPERTIES
            TIMEOUT 60
            LABELS "comprehensive;sql;s_plus"
    )
else()
    message(WARNING "No test modules found in ${CMAKE_CURRENT_LIST_DIR}")
endif()

# =========================================================
# 3. INDIVIDUAL MODULE TESTS (Unique Names)
# =========================================================

foreach(test_source ${TEST_MODULES})

    # Generate a UNIQUE target name by using the path
    # Example: "sql/column_tests.cpp" -> "test_sql_column_tests"
    string(REPLACE "/" "_" safe_name ${test_source})
    string(REPLACE "\\" "_" safe_name ${safe_name}) # Handle Windows paths
    string(REPLACE ".cpp" "" safe_name ${safe_name})
    set(target_name "test_${safe_name}")

    # Create executable
    add_executable(${target_name} ${test_source})

    # Link libraries
    target_link_libraries(${target_name} PRIVATE francodb_lib)
    if(WIN32)
        target_link_libraries(${target_name} PRIVATE ws2_32)
    endif()
    target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR}/src/include ${CMAKE_CURRENT_LIST_DIR}/framework)

    # Register test
    add_test(NAME ${target_name} COMMAND ${target_name})

endforeach()

# =========================================================
# 4. CUSTOM TARGETS
# =========================================================

add_custom_target(test_quick
        COMMAND ${CMAKE_CTEST_COMMAND} -R ComprehensiveTestSuite --output-on-failure
        COMMENT "Running comprehensive test suite..."
)

add_custom_target(test_all
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMENT "Running all tests..."
)