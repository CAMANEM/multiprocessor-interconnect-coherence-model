add_executable(
  mp_sim
  main.cpp
  top.cpp
  pe_trace_player.cpp
  l1_cache.cpp
  interconnect.cpp
  shared_memory.cpp
  monitor.cpp)

find_package(Threads REQUIRED)

target_include_directories(mp_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mp_sim PRIVATE mp_common ${MP_SYSTEMC_TARGET} Threads::Threads)
