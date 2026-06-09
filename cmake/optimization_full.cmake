set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Enable position-independent code by default (mainly relevant on non-MSVC toolchains)
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)

if(MSVC)
    add_compile_options(
        /W4              # High warnings
        /WX-             # Do NOT treat warnings as errors globally
        /MP              # Multi-core compilation
        /permissive-     # Standard conformant C++
        /EHsc            # Standard exception model
        /fp:precise      # Stable floating-point semantics (global default)
        /Zc:__cplusplus  # Correct __cplusplus reporting
    )

    # Debug configuration (unchanged)
    add_compile_options(
        $<$<CONFIG:DEBUG>:/ZI>   # Edit-and-Continue debug info
        $<$<CONFIG:DEBUG>:/Od>   # No optimizations in Debug
    )

    # Release configuration (max perf for local Tiger Lake i5-1135G7)
    add_compile_options(
        $<$<CONFIG:RELEASE>:/O2>         # Optimize for speed
        $<$<CONFIG:RELEASE>:/Ob2>        # Inline expansion (balanced; avoids /Ob2 vs /Ob3 override noise)
        $<$<CONFIG:RELEASE>:/Oi>         # Intrinsic calls
        $<$<CONFIG:RELEASE>:/Ot>         # Favor fast code
        $<$<CONFIG:RELEASE>:/GL>         # Whole Program Optimization (LTCG compile phase)
        $<$<CONFIG:RELEASE>:/Gw>         # Optimize global data
        $<$<CONFIG:RELEASE>:/Gy>         # Function-level linking
        $<$<CONFIG:RELEASE>:/GF>         # Merge identical strings
        $<$<CONFIG:RELEASE>:/Zc:inline>  # Remove unreferenced COMDAT
        $<$<CONFIG:RELEASE>:/arch:AVX2>  # Best ISA target for i5-1135G7
        $<$<CONFIG:RELEASE>:/fp:precise> # Keep deterministic/stable FP behavior

        # If you later want maximum FP speed (and accept FP behavior changes), use:
        # $<$<CONFIG:RELEASE>:/fp:fast>
    )

    # Release linker optimization
    add_link_options(
        $<$<CONFIG:RELEASE>:/LTCG>            # Link-Time Code Generation
        $<$<CONFIG:RELEASE>:/OPT:REF>         # Remove unused code/data
        $<$<CONFIG:RELEASE>:/OPT:ICF>         # Merge identical functions/data
        $<$<CONFIG:RELEASE>:/INCREMENTAL:NO>  # Non-incremental link for best optimization
        $<$<CONFIG:RELEASE>:/DEBUG:NONE>      # No PDB/debug info in Release
    )
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -fvisibility=hidden
        -fstack-protector-strong
        -fPIC
        -fexceptions
    )

    add_compile_options(
        $<$<CONFIG:DEBUG>:-O0>
        $<$<CONFIG:DEBUG>:-g>
    )

    add_compile_options(
        $<$<CONFIG:RELEASE>:-O3>
        $<$<CONFIG:RELEASE>:-fdata-sections>
        $<$<CONFIG:RELEASE>:-ffunction-sections>
        $<$<CONFIG:RELEASE>:-fPIC>
    )

    add_link_options(
        $<$<CONFIG:RELEASE>:-Wl,--gc-sections>
        $<$<CONFIG:RELEASE>:-Wl,-z,relro>
        $<$<CONFIG:RELEASE>:-Wl,-z,now>
    )
endif()
