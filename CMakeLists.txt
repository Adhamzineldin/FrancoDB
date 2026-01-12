cmake_minimum_required(VERSION 3.20)
project(FrancoDB)

# 1. Enforce C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. Professional Compiler Warnings
# We want to catch bugs early. This turns on "Wall" (All Warnings) and "Werror" (Treat warnings as errors).
# This forces you to write clean code.
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

# 3. Add the Source Directory
# This tells CMake: "Go look inside 'src' for another CMakeLists.txt and run it."
add_subdirectory(src)

# 4. Add Tests (Optional for now, but good for scalability)
add_subdirectory(test)