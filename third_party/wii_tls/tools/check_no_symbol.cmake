# Fails if a linked binary contains a symbol matching PATTERN.
#
# Proves that an application using only the TLS API does not link the HTTP
# layer: wtls_shutdown() reaches the pool through a function pointer that the
# HTTP code sets, so nothing in the core references wtls_http_*.
#
# Usage: cmake -DNM=<nm> -DFILE=<binary> -DPATTERN=<regex> -P check_no_symbol.cmake

cmake_minimum_required(VERSION 3.20)

foreach(v NM FILE PATTERN)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "check_no_symbol: -D${v}=... is required")
    endif()
endforeach()

execute_process(COMMAND "${NM}" "${FILE}" OUTPUT_VARIABLE syms RESULT_VARIABLE rc ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "check_no_symbol: ${NM} failed on ${FILE}: ${err}")
endif()
string(REPLACE ";" "," syms "${syms}")
string(REPLACE "\n" ";" lines "${syms}")
set(hits "")
foreach(line ${lines})
    if(line MATCHES "${PATTERN}")
        string(STRIP "${line}" line)
        list(APPEND hits "${line}")
    endif()
endforeach()
if(hits)
    string(REPLACE ";" "\n  " hits "${hits}")
    message(FATAL_ERROR "check_no_symbol: ${FILE} contains symbols matching '${PATTERN}':\n  ${hits}")
endif()
message(STATUS "check_no_symbol: no '${PATTERN}' in ${FILE}")
