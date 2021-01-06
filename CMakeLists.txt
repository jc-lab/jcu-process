cmake_minimum_required(VERSION 3.12)
project(jcu-process CXX)

include(FetchContent)

option(jcu_process_BUILD_TESTS "Build tests" OFF)

# ==================== SOURCE ====================

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/inc)

set(SRC_FILES)

if (WIN32)
    set(PLAT_SRC_FILES
            ${SRC_DIR}/windows/process.cc
            ${SRC_DIR}/windows/pipe_pair.h
            ${SRC_DIR}/windows/pipe_pair.cc
            )
endif(WIN32)

set(INC_FILES
        ${INC_DIR}/jcu-process/process.h
        ${INC_DIR}/jcu-process/windows_process.h
        )

add_library(jcu_process STATIC ${SRC_FILES} ${INC_FILES} ${PLAT_SRC_FILES})
target_include_directories(jcu_process
        INTERFACE
        ${INC_DIR}
        PUBLIC
        ${INC_DIR}
        )

# ================================================

if (jcu_process_BUILD_TESTS)
    set(gtest_force_shared_crt ON CACHE BOOL "")

    if (NOT TARGET gtest)
        FetchContent_Declare(
                googletest
                GIT_REPOSITORY https://github.com/google/googletest.git
                GIT_TAG release-1.10.0
        )

        FetchContent_GetProperties(googletest)

        if (NOT googletest_POPULATED)
            FetchContent_Populate(googletest)
            add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
        endif ()
    endif ()

    enable_testing()
    add_subdirectory(test)
endif ()
