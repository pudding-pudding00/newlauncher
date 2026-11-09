cmake_minimum_required(VERSION 3.20)

project(newlauncher VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/NlFlags.cmake)

# Plattformfreier Kern (Spec 4.1): nur Header/Quellen ohne libogc-Abhängigkeit.
add_library(nl_core INTERFACE)
target_include_directories(nl_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

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

    add_executable(nl_host_tests
        tests/host/main.cpp
        tests/host/core/result_test.cpp
        tests/host/core/color_test.cpp
    )
    target_include_directories(nl_host_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    target_compile_definitions(nl_host_tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
    target_link_libraries(nl_host_tests PRIVATE nl_core)
    nl_apply_flags(nl_host_tests)

    add_test(NAME nl_host_tests COMMAND nl_host_tests)
endif()
