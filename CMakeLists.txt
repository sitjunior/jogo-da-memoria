cmake_minimum_required(VERSION 3.20)
project(jogo_da_memoria CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(memory_core core/game.cpp)
target_include_directories(memory_core PUBLIC core)
if(NOT MSVC)
  target_compile_options(memory_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE memory_core)
add_test(NAME core_tests COMMAND core_tests)
