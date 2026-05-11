cmake_minimum_required(VERSION 3.16)
project(multiprocessor-interconnect-coherence-model LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(DEFINED ENV{SYSTEMC_HOME})
  list(PREPEND CMAKE_PREFIX_PATH "$ENV{SYSTEMC_HOME}")
endif()

find_package(SystemCLanguage QUIET CONFIG)
set(MP_SYSTEMC_TARGET "")
set(MP_BUILD_SIM OFF)

if(SystemCLanguage_FOUND AND TARGET SystemC::systemc)
  set(MP_SYSTEMC_TARGET SystemC::systemc)
  set(MP_BUILD_SIM ON)
  message(STATUS "Found SystemC via CMake config (SystemCLanguage)")
else()
  find_path(
    SYSTEMC_INCLUDE_DIR
    NAMES systemc
    PATHS ENV SYSTEMC_HOME
          "$ENV{SYSTEMC_HOME}"
    PATH_SUFFIXES include)

  if(NOT SYSTEMC_INCLUDE_DIR)
    find_path(SYSTEMC_INCLUDE_DIR NAMES systemc PATHS ${CMAKE_PREFIX_PATH}
                                  PATH_SUFFIXES include)
  endif()

  find_library(
    SYSTEMC_LIBRARY
    NAMES systemc libsystemc
    PATHS ENV SYSTEMC_HOME
          "$ENV{SYSTEMC_HOME}"
    PATH_SUFFIXES lib lib64 lib-msvc lib-linux lib-macosx64)

  if(NOT SYSTEMC_LIBRARY)
    find_library(
      SYSTEMC_LIBRARY
      NAMES systemc libsystemc
      PATHS ${CMAKE_PREFIX_PATH}
      PATH_SUFFIXES lib lib64 lib-msvc lib-linux lib-macosx64)
  endif()

  if(SYSTEMC_INCLUDE_DIR AND SYSTEMC_LIBRARY)
    add_library(mp_systemc_imported INTERFACE IMPORTED)
    target_include_directories(mp_systemc_imported INTERFACE "${SYSTEMC_INCLUDE_DIR}")
    target_link_libraries(mp_systemc_imported INTERFACE "${SYSTEMC_LIBRARY}")
    set(MP_SYSTEMC_TARGET mp_systemc_imported)
    set(MP_BUILD_SIM ON)
    message(STATUS "Using SystemC from SYSTEMC_HOME / CMAKE_PREFIX_PATH")
    message(STATUS "  SYSTEMC_INCLUDE_DIR=${SYSTEMC_INCLUDE_DIR}")
    message(STATUS "  SYSTEMC_LIBRARY=${SYSTEMC_LIBRARY}")
  endif()
endif()

# mp_common (trace parser shared by tools and sim)
add_library(mp_common STATIC common/trace_parser.cpp)
target_include_directories(mp_common PUBLIC "${CMAKE_SOURCE_DIR}/../include/common")

add_executable(tracegen tools/tracegen.cpp)

if(MP_BUILD_SIM)
  add_executable(
    mp_sim
    sim/main.cpp
    sim/log.cpp
    sim/top.cpp
    sim/event_log.cpp
    sim/pe_trace_player.cpp
    sim/l1_cache.cpp
    sim/interconnect.cpp
    sim/shared_memory.cpp
    sim/monitor.cpp)

  find_package(Threads REQUIRED)

  target_include_directories(mp_sim PRIVATE "${CMAKE_SOURCE_DIR}/../include/sim")
  target_link_libraries(mp_sim PRIVATE mp_common ${MP_SYSTEMC_TARGET} Threads::Threads)
else()
  message(
    WARNING
      "SystemC not found: target mp_sim will not be generated. Set SYSTEMC_HOME or "
      "CMAKE_PREFIX_PATH to a SystemC prefix (include/systemc, lib with systemc), or install a "
      "CMake package that provides SystemC::systemc.")
endif()
