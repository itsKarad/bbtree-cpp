cmake_minimum_required(VERSION 3.16)
project(bbtree_cpp LANGUAGES CXX)

add_library(bbtree INTERFACE)
target_include_directories(bbtree INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(bbtree INTERFACE cxx_std_17)

add_executable(bbtree_test tests/btree_test.cpp)
target_link_libraries(bbtree_test PRIVATE bbtree)

add_executable(bbtree_bench bench/bench.cpp)
target_link_libraries(bbtree_bench PRIVATE bbtree)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(bbtree_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(bbtree_bench PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME bbtree_test COMMAND bbtree_test)
