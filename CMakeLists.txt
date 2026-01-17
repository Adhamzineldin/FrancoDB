cmake_minimum_required(VERSION 3.10)
project(FrancoDB)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")

set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")
set(TEST_DIR "${CMAKE_SOURCE_DIR}/test")

# =========================================================
# 1. CORE LIBRARY (The Engine)
# =========================================================
file(GLOB_RECURSE FRANCODB_SOURCES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp")
file(GLOB_RECURSE FRANCODB_HEADERS CONFIGURE_DEPENDS "${SRC_DIR}/include/*.h")
# Exclude main() files from the library to avoid "multiple definition of main" errors
list(FILTER FRANCODB_SOURCES EXCLUDE REGEX ".*/src/cmd/.*")

add_library(francodb_lib ${FRANCODB_SOURCES} ${FRANCODB_HEADERS})
target_include_directories(francodb_lib PUBLIC "${SRC_DIR}/include")

# =========================================================
# 2. SERVER EXECUTABLE
# =========================================================
set(SERVER_SRC "${SRC_DIR}/cmd/server/main.cpp")
if(EXISTS "${SERVER_SRC}")
    add_executable(francodb_server "${SERVER_SRC}")
    target_link_libraries(francodb_server francodb_lib)
    if(WIN32)
        target_link_libraries(francodb_server ws2_32)
    else()
        target_link_libraries(francodb_server pthread)
    endif()
endif()

# =========================================================
# 3. SHELL EXECUTABLE (CLI)
# =========================================================
set(SHELL_SRC "${SRC_DIR}/cmd/shell/shell.cpp")
if(EXISTS "${SHELL_SRC}")
    add_executable(francodb_shell "${SHELL_SRC}")
    target_link_libraries(francodb_shell francodb_lib)
    if(WIN32)
        target_link_libraries(francodb_shell ws2_32)
    else()
        target_link_libraries(francodb_shell pthread)
    endif()
endif()

# =========================================================
# 4. UTILITIES (Setup & Service)
# =========================================================
set(SETUP_SRC "${SRC_DIR}/cmd/setup/setup.cpp")
if(EXISTS "${SETUP_SRC}")
    add_executable(francodb_setup "${SETUP_SRC}")
    if(WIN32)
        target_link_libraries(francodb_setup ws2_32)
    else()
        target_link_libraries(francodb_setup pthread)
    endif()
endif()

set(SERVICE_SRC "${SRC_DIR}/cmd/service/service_wrapper.cpp")
if(WIN32 AND EXISTS "${SERVICE_SRC}")
    add_executable(francodb_service "${SERVICE_SRC}")
    set_target_properties(francodb_service PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(francodb_service advapi32)
endif()

# =========================================================
# 5. TESTS (Delegate to test/ folder)
# =========================================================
if(EXISTS "${TEST_DIR}")
    add_subdirectory(test)
endif()

# =========================================================
# 6. WINDOWS DLL AUTO-COPY (Fixes "Missing DLL" errors)
# =========================================================
if(WIN32)
    get_filename_component(COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    file(GLOB RUNTIME_DLLS
            "${COMPILER_BIN_DIR}/libstdc++-*.dll"
            "${COMPILER_BIN_DIR}/libgcc_s_*.dll"
            "${COMPILER_BIN_DIR}/libwinpthread-*.dll"
    )
    add_custom_command(TARGET francodb_server POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${RUNTIME_DLLS}
            $<TARGET_FILE_DIR:francodb_server>
            COMMENT "Auto-copying MinGW Runtime DLLs..."
    )
endif()