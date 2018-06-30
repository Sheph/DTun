cmake_minimum_required(VERSION 2.6)
cmake_policy(SET CMP0014 NEW)

project(DTUN)

#platform setup

if (CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: None (CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif ()

#out dir

set(DTUN_OUT_DIR ${DTUN_BINARY_DIR}/out)

#set includes/libs

set(DTUN_INCLUDE_DIR "${DTUN_SOURCE_DIR}/include")
include_directories(${DTUN_INCLUDE_DIR})

#find packages

set(Boost_USE_MULTITHREADED ON)

find_package(Threads)
find_package(PkgConfig)
find_package(Boost COMPONENTS system thread REQUIRED)

#fix output directories

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${DTUN_OUT_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${DTUN_OUT_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${DTUN_OUT_DIR}/lib)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-deprecated-declarations -D_REENTRANT -fPIC -DPIC")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-deprecated-declarations -D_REENTRANT -fPIC -DPIC")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined -Wl,--exclude-libs,ALL")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-undefined -Wl,--exclude-libs,ALL")

add_definitions(${Boost_LIB_DIAGNOSTIC_DEFINITIONS})

add_subdirectory(dnode)
