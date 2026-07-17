add_executable(fbsampler-tests
    version_test.cpp
    model_validate_test.cpp
    golden/model_golden_test.cpp
)

target_link_libraries(fbsampler-tests
    PRIVATE
        sampler-core
        Catch2::Catch2WithMain
)

target_compile_definitions(fbsampler-tests
    PRIVATE
        FBSAMPLER_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
)

include(Catch)
catch_discover_tests(fbsampler-tests)

# AD-6 guard negative test: proves fbsampler_assert_no_gui_modules() actually
# fires by configuring an isolated fixture that links a fake GUI module and
# expecting that configure to fail.
add_test(NAME ad6_guard_negative
    COMMAND ${CMAKE_COMMAND}
        -S "${CMAKE_CURRENT_SOURCE_DIR}/ad6_guard_negative"
        -B "${CMAKE_CURRENT_BINARY_DIR}/ad6_guard_negative_build"
)
set_tests_properties(ad6_guard_negative PROPERTIES
    PASS_REGULAR_EXPRESSION "AD-6 violation"
)
