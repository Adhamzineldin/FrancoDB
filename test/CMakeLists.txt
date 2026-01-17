# =========================================================
# 1. UNIT TESTS
# =========================================================

# Thread Pool Test
# Assuming you put the threadpool test in src/cmd/test/threadpool_test.cpp based on previous steps
# If it is in test/unit/threadpool_test.cpp, adjust the path below.
set(THREADPOOL_TEST_SRC "${CMAKE_SOURCE_DIR}/src/cmd/test/threadpool_test.cpp")

if(EXISTS "${THREADPOOL_TEST_SRC}")
    add_executable(test_threadpool "${THREADPOOL_TEST_SRC}")
    target_link_libraries(test_threadpool francodb_lib)
    if(WIN32)
        target_link_libraries(test_threadpool ws2_32)
    else()
        target_link_libraries(test_threadpool pthread)
    endif()
endif()


# =========================================================
# 2. SYSTEM / STRESS TESTS
# =========================================================

# Stress Client
# Assumes file is at: test/system/stress_client.cpp
set(STRESS_CLIENT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/system/stress_client.cpp")

if(EXISTS "${STRESS_CLIENT_SRC}")
    add_executable(stress_client "${STRESS_CLIENT_SRC}")

    # Link to FrancoDB lib (if you use its headers)
    target_link_libraries(stress_client francodb_lib)

    # CRITICAL FIX: Link Windows Sockets
    if(WIN32)
        target_link_libraries(stress_client ws2_32)
    else()
        target_link_libraries(stress_client pthread)
    endif()
endif()


add_executable(consistency_client system/consistency_client.cpp)
if(WIN32)
    target_link_libraries(consistency_client PRIVATE ws2_32)
endif()
target_link_libraries(consistency_client PRIVATE francodb_lib)