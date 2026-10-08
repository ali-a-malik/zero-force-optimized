cmake_minimum_required(VERSION 3.16)
project(rzf_supply_chain CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(RZF_NATIVE_ARCH "Build with -march=native (off for portable binaries)" ON)
option(RZF_OPENMP "Parallelise the exact DP over popcount layers when available" ON)

add_library(rzf_core STATIC
  engine/rzf_core.cpp
  engine/rzf_families.cpp
  engine/rzf_mc.cpp
)
target_include_directories(rzf_core PUBLIC engine)
target_compile_options(rzf_core PRIVATE -Wall -Wextra)

if(RZF_NATIVE_ARCH)
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-march=native RZF_HAS_MARCH_NATIVE)
  if(RZF_HAS_MARCH_NATIVE)
    target_compile_options(rzf_core PUBLIC -march=native)
  endif()
endif()

if(RZF_OPENMP)
  find_package(OpenMP)
  if(OpenMP_CXX_FOUND)
    target_link_libraries(rzf_core PUBLIC OpenMP::OpenMP_CXX)
    message(STATUS "OpenMP found: the exact DP will run layers in parallel")
  else()
    message(STATUS "OpenMP not found: the exact DP will run single-threaded")
  endif()
endif()

add_executable(rzf_cli engine/cli.cpp)
target_link_libraries(rzf_cli PRIVATE rzf_core)

add_executable(rzf_verify engine/tests/verify.cpp)
target_link_libraries(rzf_verify PRIVATE rzf_core)

# §6 of SUPPLY_CHAIN_PLAN.md. Reads the research CSVs from throttling/.
enable_testing()
add_test(NAME verify
         COMMAND rzf_verify --data ${CMAKE_CURRENT_SOURCE_DIR}/throttling)
