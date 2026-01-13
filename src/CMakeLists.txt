# Find all cpp files
file(GLOB_RECURSE FRANCODB_SOURCES CONFIGURE_DEPENDS
        "*.cpp"
)

# Remove main.cpp from library sources
list(FILTER FRANCODB_SOURCES EXCLUDE REGEX "main.cpp")

# Find all header files
file(GLOB_RECURSE FRANCODB_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

# Create library
add_library(francodb_lib
        ${FRANCODB_SOURCES}
        ${FRANCODB_HEADERS}
)

# Include paths
target_include_directories(francodb_lib
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Executable
add_executable(FrancoDB main.cpp
        include/buffer/lru_replacer.h
        include/buffer/buffer_pool_manager.h
        buffer/lru_replacer.cpp
        buffer/buffer_pool_manager.cpp
        include/buffer/replacer.h
        include/buffer/clock_replacer.h
        buffer/clock_replacer.cpp
        include/storage/page/b_plus_tree_page.h
        storage/page/b_plus_tree_page.cpp
        include/storage/page/b_plus_tree_internal_page.h
        "storage/page/b_plus_tree_internal_page.cpp"
        include/common/exception.h
        include/common/rid.h
        include/storage/page/b_plus_tree_leaf_page.h
        storage/page/b_plus_tree_leaf_page.cpp
        "include/storage/index/b_plus_tree.h"
        include/concurrency/transaction.h
        include/storage/index/index_iterator.h
        storage/index/b_plus_tree.cpp
        include/storage/table/tuple.h
        include/storage/table/table_page.h
        storage/table/table_page.cpp
        include/storage/table/table_heap.h
        storage/table/table_heap.cpp)
target_link_libraries(FrancoDB francodb_lib)
