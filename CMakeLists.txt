cmake_minimum_required(VERSION 3.28)

project(feedback-sampler
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(fbsampler_deps)

add_subdirectory(core)
add_subdirectory(plugin)

enable_testing()
add_subdirectory(tests)

include(fbsampler_guards)
# AD-6: sampler-core must never link a JUCE GUI module. Configure fails if one appears.
fbsampler_assert_no_gui_modules(sampler-core)
