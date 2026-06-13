cmake_minimum_required(VERSION 3.21)
project(mith-atomas
    VERSION 0.1.0
    DESCRIPTION "Swarm robotics orchestration runtime"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Options (see ARCHITECTURE.md §11) ---
option(MITH_BUILD_EXAMPLES "Build example programs"    ON)
option(MITH_BUILD_TESTS    "Build test suite"          ON)
option(MITH_BUILD_SIM      "Build simulation harness"  ON)
option(MITH_ENABLE_UDP     "Build UDP transport"       ON)
option(MITH_ENABLE_SERIAL  "Build serial transport"    OFF)

# --- Core library ---
# Header-only INTERFACE target during the Pre-v0.1 design phase.
# Will switch to a compiled library once src/ is populated per v0.1 roadmap.
add_library(mith INTERFACE)
add_library(mith::mith ALIAS mith)

target_include_directories(mith INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(mith INTERFACE cxx_std_17)

# --- Subdirectories (enabled as content lands) ---
if(MITH_BUILD_EXAMPLES AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt)
    add_subdirectory(examples)
endif()

if(MITH_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    enable_testing()
    add_subdirectory(tests)
endif()
