cmake_minimum_required(VERSION 3.28)

project(feedback-sampler
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
# Also pin the CACHE entry: vendored sfizz's SfizzConfig.cmake does
# `set(CMAKE_CXX_STANDARD 14 CACHE STRING ...)`, which under CMP0126 OLD
# removes the normal variable above and drops sfizz (and its ghc/fs_std.hpp
# `fs` alias) to C++14 on GCC/Clang while MSVC gets force-bumped to 17. That
# split makes sfizz's Parser API symbols use ghc::filesystem::path on
# Linux/mac but std::filesystem::path on Windows — and sampler-core (C++17
# everywhere) then fails to link against sfizz::parser on Linux/mac. With the
# cache entry pre-set to 17, sfizz's non-FORCE cache set is a no-op and every
# platform agrees on std::filesystem.
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to be used" FORCE)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Static CRT on MSVC for every target (us and vendored deps alike): avoids
# shipping/depending on a matching MSVC redistributable on end-user machines,
# and matches what vendored sfizz already forces for itself in
# SfizzConfig.cmake — set here, before any FetchContent/add_subdirectory, so
# JUCE/Catch2/sfizz all inherit the same choice and don't LNK2038 against
# each other over a /MT vs /MD mismatch.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(fbsampler_deps)

add_subdirectory(core)
add_subdirectory(plugin)

enable_testing()
add_subdirectory(tests)

include(fbsampler_guards)
# AD-6: sampler-core must never link a JUCE GUI module. Configure fails if one appears.
fbsampler_assert_no_gui_modules(sampler-core)
