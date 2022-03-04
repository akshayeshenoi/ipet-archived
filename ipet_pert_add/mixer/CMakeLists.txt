# This project measures the performance of the privacy component of iPET.
# We run the core mixing part inside shadow and analyse the privacy performance.
# The main advantage is that we get to control time inside shadow and can play large pcap files
# inside the program relatively quickly.

project(shadow-plugin-ipet_mixer)
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

macro(add_cflags)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARGN}")
endmacro(add_cflags)

# ### shadow stuff ###
# we need to know where shadow is installed
if("${SHADOW_ROOT}" STREQUAL "")
    set(SHADOW_ROOT "$ENV{HOME}/.shadow")
endif("${SHADOW_ROOT}" STREQUAL "")
if(EXISTS "${SHADOW_ROOT}")
    message(STATUS "SHADOW_ROOT = ${SHADOW_ROOT}")
else()
    message(FATAL_ERROR "SHADOW_ROOT path does not exist: '${SHADOW_ROOT}'")
endif()

set(CMAKE_MODULE_PATH "${SHADOW_ROOT}/share/cmake/Modules")
if(EXISTS "${CMAKE_MODULE_PATH}")
    message(STATUS "CMAKE_MODULE_PATH = ${CMAKE_MODULE_PATH}")
else()
    message(FATAL_ERROR "CMAKE_MODULE_PATH path does not exist: '${CMAKE_MODULE_PATH}'")
endif()
include_directories(${CMAKE_MODULE_PATH})
include(ShadowTools)

include(TestBigEndian)
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)

# ### end shadow stuff ###

## plug-ins need to disable fortification to ensure syscalls are intercepted
add_cflags("-fPIC -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE")

set(CMAKE_THREAD_PREFER_PTHREAD 1)
set(CMAKE_CXX_STANDARD 11)
find_package(Threads REQUIRED)
message(STATUS "Using Threads library: ${CMAKE_THREAD_LIBS_INIT}")

## create and install a dynamic library that can plug into shadow
add_shadow_plugin(shadow-plugin-ipet_mixer mixer.cpp main.cpp)
target_link_libraries(shadow-plugin-ipet_mixer Threads::Threads)
install(TARGETS shadow-plugin-ipet_mixer DESTINATION ${SHADOW_ROOT}/plugins)

## create exe for testing
add_executable(shadow-plugin-ipet_mixer-exe mixer.cpp main.cpp)
target_link_libraries(shadow-plugin-ipet_mixer-exe Threads::Threads)
