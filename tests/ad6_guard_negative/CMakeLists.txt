# Negative-test fixture for the AD-6 guard (cmake/fbsampler_guards.cmake).
# Configured standalone (see ../CMakeLists.txt's ad6_guard_negative ctest entry):
# links a fake "juce_gui_basics" target and expects configure to fail with
# FATAL_ERROR, proving fbsampler_assert_no_gui_modules() actually fires.

cmake_minimum_required(VERSION 3.28)
project(ad6_guard_negative_fixture LANGUAGES NONE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
include(fbsampler_guards)

add_library(juce_gui_basics INTERFACE)
add_library(fixture_target INTERFACE)
target_link_libraries(fixture_target INTERFACE juce_gui_basics)

fbsampler_assert_no_gui_modules(fixture_target)
