# CMake toolchain for building libwiitls (and Wii consumer projects) directly
# from native Windows using devkitPPC + Ninja (no MSYS2 wrapper shell needed).
#
# Example:
#   cmake -S . -B build-wii-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/wii-win-toolchain.cmake
#   cmake --build build-wii-win
#
# Note: Do not wrap this file in include_guard(); CMake runs toolchain files in
# sub-invocations (compiler inspection / try_compile).

set(_wtls_dkp "")
if(DEFINED WTLS_DEVKITPRO AND EXISTS "${WTLS_DEVKITPRO}/cmake/Wii.cmake")
    file(TO_CMAKE_PATH "${WTLS_DEVKITPRO}" _wtls_dkp)
elseif(DEFINED ENV{DEVKITPRO})
    file(TO_CMAKE_PATH "$ENV{DEVKITPRO}" _wtls_env_dkp)
    if(EXISTS "${_wtls_env_dkp}/cmake/Wii.cmake")
        set(_wtls_dkp "${_wtls_env_dkp}")
    elseif(_wtls_env_dkp MATCHES "^/([a-zA-Z])/(.*)$")
        set(_wtls_win_dkp "${CMAKE_MATCH_1}:/${CMAKE_MATCH_2}")
        if(EXISTS "${_wtls_win_dkp}/cmake/Wii.cmake")
            set(_wtls_dkp "${_wtls_win_dkp}")
        endif()
    endif()
endif()
if(NOT _wtls_dkp)
    foreach(_cand "C:/devkitPro" "D:/devkitPro")
        if(EXISTS "${_cand}/cmake/Wii.cmake")
            set(_wtls_dkp "${_cand}")
            break()
        endif()
    endforeach()
endif()
if(NOT _wtls_dkp)
    message(FATAL_ERROR "Cannot locate devkitPro Wii CMake support. Set WTLS_DEVKITPRO or DEVKITPRO (e.g. C:/devkitPro).")
endif()

set(ENV{DEVKITPRO} "${_wtls_dkp}")
if(EXISTS "${_wtls_dkp}/devkitPPC/bin")
    set(ENV{DEVKITPPC} "${_wtls_dkp}/devkitPPC")
endif()

# devkitPro's dkp-initialize-path.cmake aborts when CMAKE_HOST_WIN32 is set;
# mask it cleanly only while loading Wii.cmake.
set(_wtls_saved_host_win32 "${CMAKE_HOST_WIN32}")
unset(CMAKE_HOST_WIN32)
include("${_wtls_dkp}/cmake/Wii.cmake")
if(_wtls_saved_host_win32)
    set(CMAKE_HOST_WIN32 TRUE)
endif()
