# 1. Enable Testing Dashboard
enable_testing()

# 2. Dynamically find all .cpp files inside test/ (and subdirectories like test/storage/)
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "*.cpp"
)

# 3. Loop through each file and create a separate test executable
foreach(test_source ${TEST_SOURCES})
    # Extract the filename without extension (e.g., "storage/disk_test.cpp" -> "disk_test")
    get_filename_component(test_name ${test_source} NAME_WE)

    # Create the executable (e.g., bin/disk_test)
    add_executable(${test_name} ${test_source}
            buffer/buffer_pool_test.cpp)

    # Link it to your database library
    target_link_libraries(${test_name} PRIVATE francodb_lib)

    # Register it with CTest so you can run "ctest"
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()