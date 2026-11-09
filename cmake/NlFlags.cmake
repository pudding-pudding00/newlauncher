# Gemeinsame Compiler-Flags für Wii- und Host-Code (Spec 2: C++20 ohne Exceptions/RTTI).
function(nl_apply_flags target)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
        -Wall -Wextra
    )
endfunction()
