cmake_minimum_required(VERSION 3.20)

project(newlauncher VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/NlFlags.cmake)

# Plattformfreier Kern (Spec 4.1): ohne libogc, auf dem Host getestet, auf der Wii mitgelinkt.
add_library(nl_core STATIC
    src/core/assert.cpp
    src/core/theme.cpp
    src/core/console.cpp
    src/gfx/draw_list.cpp
    src/gfx/shapes.cpp
    src/gfx/atlas.cpp
)
target_include_directories(nl_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
nl_apply_flags(nl_core)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoWii")
    add_executable(newlauncher
        src/app/main.cpp
        src/platform/log.cpp
        src/platform/video.cpp
        src/gfx/gx_context.cpp
    )
    target_link_libraries(newlauncher PRIVATE nl_core wiiuse bte)
    target_compile_definitions(newlauncher PRIVATE NL_VERSION="${PROJECT_VERSION}")
    nl_apply_flags(newlauncher)
    ogc_create_dol(newlauncher)
else()
    enable_testing()

    file(GLOB_RECURSE NL_HOST_TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/host/*_test.cpp)
    add_executable(nl_host_tests tests/host/main.cpp ${NL_HOST_TEST_SOURCES})
    target_include_directories(nl_host_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/host)
    target_compile_definitions(nl_host_tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
    target_link_libraries(nl_host_tests PRIVATE nl_core)
    nl_apply_flags(nl_host_tests)

    add_test(NAME nl_host_tests COMMAND nl_host_tests)
endif()
