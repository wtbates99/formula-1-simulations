cmake_minimum_required(VERSION 3.21)
project(f1sim LANGUAGES CXX)

option(F1SIM_BUILD_WASM "Build WebAssembly module with Emscripten" OFF)
option(F1SIM_BUILD_CLI "Build native CLI runner" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  add_compile_options($<$<CONFIG:Release>:-O3> $<$<CONFIG:Release>:-ffast-math> $<$<CONFIG:Release>:-fno-math-errno>)
  add_compile_options($<$<CONFIG:RelWithDebInfo>:-O3> $<$<CONFIG:RelWithDebInfo>:-g>)
endif()

add_subdirectory(sim)
