add_library(mp_common STATIC trace_parser.cpp)
target_include_directories(mp_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
