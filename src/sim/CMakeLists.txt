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

# Headers: ../include/sim (model), ../include/common (trace parser); common also via mp_common link.
target_include_directories(mp_sim PRIVATE "${CMAKE_SOURCE_DIR}/../include/sim")
target_link_libraries(mp_sim PRIVATE mp_common ${MP_SYSTEMC_TARGET} Threads::Threads)
