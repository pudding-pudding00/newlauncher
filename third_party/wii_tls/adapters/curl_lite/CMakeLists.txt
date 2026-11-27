add_library(wiitls_curl STATIC wiitls_curl.c)
target_include_directories(wiitls_curl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(wiitls_curl PUBLIC wiitls::wiitls)
target_compile_options(wiitls_curl PRIVATE -Wall -Wextra)
add_library(wiitls::curl ALIAS wiitls_curl)
