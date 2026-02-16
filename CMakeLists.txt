cmake_minimum_required(VERSION 3.20)
project(f1_simulations LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(F1SIM_BUILD_VIEWER "Build raylib visual viewer" ON)

find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)

add_library(f1sim_core
    src/sim/simulator.cpp
)
target_include_directories(f1sim_core PUBLIC include)

add_library(f1sim_support
    src/support/scenario_loader.cpp
    src/support/telemetry_seed.cpp
    src/support/replay_logger.cpp
)
target_include_directories(f1sim_support PUBLIC include)
target_link_libraries(f1sim_support PUBLIC f1sim_core SQLite::SQLite3)

add_executable(sim_cli apps/sim_cli.cpp)
target_link_libraries(sim_cli PRIVATE f1sim_core f1sim_support SQLite::SQLite3)

add_executable(telemetry_ingest main.cpp)
target_link_libraries(telemetry_ingest PRIVATE SQLite::SQLite3 CURL::libcurl)

if(F1SIM_BUILD_VIEWER)
    find_package(raylib QUIET)
    if(raylib_FOUND)
        add_executable(sim_viewer apps/sim_viewer.cpp)
        target_link_libraries(sim_viewer PRIVATE f1sim_core f1sim_support SQLite::SQLite3 raylib)
        target_compile_definitions(sim_viewer PRIVATE F1SIM_HAS_RAYLIB=1)
    else()
        message(WARNING "raylib not found. sim_viewer target will not be built.")
    endif()
endif()
