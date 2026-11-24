# Constant-time check for src/accel/x25519_fe32.c on the Wii target.
#
# The source swaps and selects with masks, but GCC may turn a mask built from
# a 0/1 value back into a branch (it did: -O3 for the PPC750 compiled the
# ladder's cswap into beq). This script disassembles the object and counts the
# conditional branches in the functions that handle the secret scalar. CTR
# loops (bdnz/bdz) depend only on the trip count and are free; every other
# conditional branch must fit the function's budget: the ladder's one loop
# over the bit position, nothing in ge_select. A new branch fails the build;
# look at it before raising a budget.
#
# Usage: cmake -DOBJDUMP=<powerpc-eabi-objdump> -DOBJECTS=<obj;...> -P check_ct.cmake

cmake_minimum_required(VERSION 3.20) # script mode starts with no policies (IN_LIST needs CMP0057)
# OBJECTS may hold all of a target's objects; x25519_fe32 is picked out.

set(funcs x25519_ladder ge_select)
set(budget_x25519_ladder 1) # loop over pos 254..0
set(budget_ge_select 0)

set(obj "")
foreach(o ${OBJECTS})
    if(o MATCHES "x25519_fe32\\.c?\\.?o(bj)?$")
        set(obj "${o}")
    endif()
endforeach()
if(NOT obj)
    message(FATAL_ERROR "check_ct: x25519_fe32 object not found in: ${OBJECTS}")
endif()

execute_process(COMMAND "${OBJDUMP}" -d "${obj}" OUTPUT_VARIABLE dis RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "check_ct: ${OBJDUMP} failed on ${obj}")
endif()
string(REPLACE ";" "," dis "${dis}")
string(REPLACE "\n" ";" lines "${dis}")

set(current "")
set(seen "")
set(bad "")
foreach(f ${funcs})
    set(n_${f} 0)
endforeach()
foreach(line ${lines})
    if(line MATCHES "^[0-9a-f]+ <([A-Za-z0-9_.]+)>:")
        set(current "${CMAKE_MATCH_1}")
        if(current IN_LIST funcs)
            list(APPEND seen "${current}")
        endif()
        continue()
    endif()
    if(NOT current IN_LIST funcs)
        continue()
    endif()
    # "  abc:  41 82 00 2c   beq     948 <x25519_ladder+0x948>"
    if(line MATCHES "^ *([0-9a-f]+):[ \t]+[0-9a-f ]+[ \t]+(b[a-z]*)[-+]?[ \t]+(cr[0-7],)?([0-9a-f]+) <")
        set(op "${CMAKE_MATCH_2}")
        if(op MATCHES "^(b|bl|bla|ba|bdnz|bdz)$") # unconditional, or a CTR loop
            continue()
        endif()
        math(EXPR n_${current} "${n_${current}} + 1")
        string(STRIP "${line}" line)
        list(APPEND bad "${current}: ${line}")
    endif()
endforeach()

foreach(f ${funcs})
    if(NOT f IN_LIST seen)
        message(FATAL_ERROR "check_ct: function ${f} not found in ${obj} (inlined or renamed? update tools/check_ct.cmake)")
    endif()
endforeach()
set(over "")
foreach(f ${funcs})
    if(n_${f} GREATER budget_${f})
        list(APPEND over "${f} has ${n_${f}} (budget ${budget_${f}})")
    endif()
endforeach()
if(over)
    string(REPLACE ";" ", " over "${over}")
    string(REPLACE ";" "\n  " bad "${bad}")
    message(FATAL_ERROR "check_ct: conditional branches in secret-dependent code: ${over}:\n  ${bad}")
endif()
message(STATUS "check_ct: x25519_ladder, ge_select within their branch budgets")
