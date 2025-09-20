cmake_minimum_required(VERSION 3.15)
project(rdma_dm_sim CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find yaml-cpp (install via your package manager or vcpkg)
find_package(yaml-cpp REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(simlib
  src/event_loop.cc
  src/config.cc
  src/rdma.cc
  src/cache.cc
  src/locks.cc
  src/rdwc.cc
  src/hopscotch.cc
  src/index_sherman.cc
  src/workload.cc)

target_link_libraries(simlib PUBLIC yaml-cpp)

target_include_directories(simlib PUBLIC include)

add_executable(sim src/main.cc)

target_link_libraries(sim PRIVATE simlib yaml-cpp)
