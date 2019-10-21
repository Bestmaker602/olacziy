cmake_minimum_required(VERSION 3.10)
project(magic_enum)

set(TARGET magic_enum)
set(PUBLIC_HDR_DIR include)

# ==================================================================================================
# Sources and headers
# ==================================================================================================
set(PUBLIC_HDRS ${PUBLIC_HDR_DIR}/${TARGET}/magic_enum.h)
set(SRCS dummy.cpp)

# ==================================================================================================
# Include and target definitions
# ==================================================================================================
include_directories(${PUBLIC_HDR_DIR})

add_library(${TARGET} STATIC ${PUBLIC_HDRS} ${SRCS})
target_include_directories(${TARGET} PUBLIC ${PUBLIC_HDR_DIR})
