# Vendored dependencies, pinned to exact commits (see README "Pinned dependencies").
#
# sfizz upstream release cadence is stalled (1.2.3, Jan 2024) and local patches
# are expected later; FetchContent with an exact SHA keeps the pin in-repo and
# tolerates patching via PATCH_COMMAND when the time comes.

include(FetchContent)

# --- sfizz 1.2.3 (tag 1.2.3) ---
set(SFIZZ_JACK OFF CACHE BOOL "" FORCE)
set(SFIZZ_RENDER OFF CACHE BOOL "" FORCE)
set(SFIZZ_SHARED OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEMOS OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEVTOOLS OFF CACHE BOOL "" FORCE)
set(SFIZZ_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SFIZZ_TESTS OFF CACHE BOOL "" FORCE)
# Local patches on top of 1.2.3 (see cmake/patches/sfizz-1.2.3-fixes.patch):
#   - SfizzConfig.cmake: don't apply ARM32 -mfpu/-mfloat-abi flags on arm64/aarch64 (macOS CI)
#   - Voice.cpp: replace 0x1.fffffep-1 hexfloat literal (rejected by newer GCC) with 0.99999994f
FetchContent_Declare(sfizz
    GIT_REPOSITORY https://github.com/sfztools/sfizz.git
    GIT_TAG 4e70dc0bef53b41f2853ed46e26f5911114c92d0 # 1.2.3
    PATCH_COMMAND ${CMAKE_COMMAND}
        -DPATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/sfizz-1.2.3-fixes.patch
        -P ${CMAKE_CURRENT_LIST_DIR}/patches/apply_patch.cmake
    UPDATE_DISCONNECTED ON
)

# --- JUCE 8.0.14 (tag 8.0.14) ---
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 2cdfca8feb300fb424002ba2c2751569e5bacb64 # 8.0.14
    GIT_SHALLOW OFF
)

# --- Catch2 3.8.1 (tag v3.8.1) ---
FetchContent_Declare(catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG 2b60af89e23d28eefc081bc930831ee9d45ea58b # v3.8.1
)

FetchContent_MakeAvailable(sfizz juce catch2)

# sfizz's vendored atomic_queue (git submodule, unpatched) uses `template foo` without
# an argument list — an error on AppleClang 16+ (-Wmissing-template-arg-list-after-template-kw).
# Scoped suppression; drop when sfizz updates atomic_queue.
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    foreach(_sfizz_target sfizz_internal sfizz_static sfizz)
        if(TARGET ${_sfizz_target})
            target_compile_options(${_sfizz_target} PRIVATE
                -Wno-missing-template-arg-list-after-template-kw)
        endif()
    endforeach()
endif()
