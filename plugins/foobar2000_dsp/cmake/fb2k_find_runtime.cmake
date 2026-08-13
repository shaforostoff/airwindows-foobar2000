# ---------------------------------------------------------------------------
# fb2k_find_runtime.cmake
#
# Locates an installed foobar2000 whose shared.dll matches the architecture we
# are building for. Only the component smoke test needs this; the component
# itself builds without foobar2000 being installed at all.
#
# Sets FB2K_RUNTIME_DIR (or leaves it empty).
# ---------------------------------------------------------------------------

#! Reads the COFF machine type out of a PE file, e.g. 0x8664 or 0x014c.
function(fb2k_pe_machine path out_var)
    set(${out_var} "" PARENT_SCOPE)
    if(NOT EXISTS "${path}")
        return()
    endif()
    file(READ "${path}" hdr HEX LIMIT 4096)
    string(LENGTH "${hdr}" len)
    if(len LESS 128)
        return()
    endif()

    # e_lfanew: little endian DWORD at 0x3C.
    string(SUBSTRING "${hdr}" 120 8 lfanew)
    string(SUBSTRING "${lfanew}" 0 2 b0)
    string(SUBSTRING "${lfanew}" 2 2 b1)
    string(SUBSTRING "${lfanew}" 4 2 b2)
    string(SUBSTRING "${lfanew}" 6 2 b3)
    math(EXPR pe_off "0x${b3}${b2}${b1}${b0}")

    # Machine: little endian WORD at e_lfanew + 4 (right after "PE\0\0").
    math(EXPR mpos "(${pe_off} + 4) * 2")
    math(EXPR mend "${mpos} + 4")
    if(mend GREATER len)
        return()
    endif()
    string(SUBSTRING "${hdr}" ${mpos} 4 m)
    string(SUBSTRING "${m}" 0 2 m0)
    string(SUBSTRING "${m}" 2 2 m1)
    set(${out_var} "0x${m1}${m0}" PARENT_SCOPE)
endfunction()

function(fb2k_find_runtime target_arch out_var)
    set(${out_var} "" PARENT_SCOPE)

    if(target_arch STREQUAL "x64")
        set(want "0x8664")
    elseif(target_arch STREQUAL "ARM64EC")
        set(want "0xa641")
    else()
        set(want "0x014c")
    endif()

    set(candidates "")
    if(DEFINED ENV{FOOBAR2000_DIR})
        list(APPEND candidates "$ENV{FOOBAR2000_DIR}")
    endif()
    foreach(root "$ENV{ProgramFiles}" "$ENV{ProgramFiles\(x86\)}" "$ENV{LOCALAPPDATA}")
        if(root)
            list(APPEND candidates "${root}/foobar2000" "${root}/foobar2000 v2")
        endif()
    endforeach()

    foreach(dir ${candidates})
        fb2k_pe_machine("${dir}/shared.dll" machine)
        if(machine STREQUAL want)
            set(${out_var} "${dir}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()
