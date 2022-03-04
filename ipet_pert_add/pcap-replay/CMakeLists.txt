# this project simply replays the pcap files
project(shadow-plugin-pcap_replay_ipet)
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

### shadow stuff ###
macro(add_cflags)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARGN}")
endmacro(add_cflags)

# we need to know where shadow is installed
# if("${SHADOW_ROOT}" STREQUAL "")
#     set(SHADOW_ROOT "$ENV{HOME}/.shadow")
# endif("${SHADOW_ROOT}" STREQUAL "")
# if(EXISTS "${SHADOW_ROOT}")
#     message(STATUS "SHADOW_ROOT = ${SHADOW_ROOT}")
# else()
#     message(FATAL_ERROR "SHADOW_ROOT path does not exist: '${SHADOW_ROOT}'")
# endif()

# set(CMAKE_MODULE_PATH "${SHADOW_ROOT}/share/cmake/Modules")
# if(EXISTS "${CMAKE_MODULE_PATH}")
#     message(STATUS "CMAKE_MODULE_PATH = ${CMAKE_MODULE_PATH}")
# else()
#     message(FATAL_ERROR "CMAKE_MODULE_PATH path does not exist: '${CMAKE_MODULE_PATH}'")
# endif()
# include_directories(${CMAKE_MODULE_PATH})
# include(ShadowTools)

# include(TestBigEndian)
# TEST_BIG_ENDIAN(IS_BIG_ENDIAN)

### end shadow stuff ###

# dependencies
# find_package(GLIB REQUIRED)
# include_directories(AFTER ${GLIB_INCLUDES})
find_library(GLIB_LIBRARIES NAMES glib-2.0 REQUIRED)
find_path(GLIB_INCLUDE_DIR NAMES glib.h PATH_SUFFIXES glib-2.0 REQUIRED)

## plug-ins need to disable fortification to ensure syscalls are intercepted
add_cflags("-fPIC -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE")

## create and install a dynamic library that can plug into shadow
# add_shadow_plugin(shadow-plugin-pcap_replay_ipet pcap_replay-main.c pcap_replay.c)
# target_link_libraries(shadow-plugin-pcap_replay_ipet ${GLIB_LIBRARIES} -lpcap)
# install(TARGETS shadow-plugin-pcap_replay_ipet DESTINATION ${SHADOW_ROOT}/plugins)

## create exe for testing
# add_shadow_exe(shadow-plugin-pcap_replay_ipet-exe pcap_replay-main.c pcap_replay.c)
add_executable(shadow-plugin-pcap_replay_ipet-exe pcap_replay-main.c pcap_replay.c)
target_link_libraries(shadow-plugin-pcap_replay_ipet-exe ${GLIB_LIBRARIES} -lpcap)

## simple server stuff
# add_shadow_plugin(shadow-plugin-simple_server simple-server.cpp)
# install(TARGETS shadow-plugin-simple_server DESTINATION ${SHADOW_ROOT}/plugins)

## create exe for testing
add_executable(shadow-plugin-simple_server-exe simple-server.cpp)
