cmake_minimum_required(VERSION 3.20)
project(lowlat_orderbook CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

include(FetchContent)
FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
FetchContent_Declare(benchmark URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip)
set(BENCHMARK_ENABLE_TESTING OFF)
FetchContent_MakeAvailable(googletest benchmark)

add_library(orderbook INTERFACE)                 # header-only for now; add src/*.cpp later
target_include_directories(orderbook INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

enable_testing()
include(GoogleTest)

add_executable(test_types tests/test_types.cpp)
target_link_libraries(test_types PRIVATE orderbook GTest::gtest_main)
gtest_discover_tests(test_types)

add_executable(test_price_level tests/test_price_level.cpp)
target_link_libraries(test_price_level PRIVATE orderbook GTest::gtest_main)
gtest_discover_tests(test_price_level)

add_executable(test_order_book tests/test_order_book.cpp)
target_link_libraries(test_order_book PRIVATE orderbook GTest::gtest_main)
gtest_discover_tests(test_order_book)

add_executable(test_arena tests/test_arena.cpp)
target_link_libraries(test_arena PRIVATE orderbook GTest::gtest_main)
gtest_discover_tests(test_arena)
