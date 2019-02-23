project(mikktspace)
cmake_minimum_required(VERSION 3.0)

add_library(mikktspace STATIC mikktspace.c)
target_include_directories(mikktspace INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

install(TARGETS mikktspace ARCHIVE DESTINATION lib/${DIST_DIR})
