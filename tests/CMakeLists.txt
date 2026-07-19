add_executable(fbsampler-tests
    version_test.cpp
    model_validate_test.cpp
    sfz_frontend_test.cpp
    golden/model_golden_test.cpp
    golden/sfz_golden_test.cpp
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

# --- SFZ fuzz harness (Story 1.3, NFR-4/AD-10) ---
# Every platform runs the deterministic corpus-replay + mutation runner as a
# regular ctest test. With -DFBSAMPLER_LIBFUZZER=ON (clang only) the same
# LLVMFuzzerTestOneInput entry point builds against libFuzzer instead for
# coverage-guided exploration.
option(FBSAMPLER_LIBFUZZER "Build the SFZ fuzz harness with libFuzzer (requires clang)" OFF)
set(FBSAMPLER_FUZZ_CORPUS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/sfz/corpus")

if(FBSAMPLER_LIBFUZZER)
    add_executable(sfz-fuzzer fuzz/sfz/sfz_fuzzer.cpp)
    target_link_libraries(sfz-fuzzer PRIVATE sampler-core)
    target_compile_options(sfz-fuzzer PRIVATE -fsanitize=fuzzer)
    target_link_options(sfz-fuzzer PRIVATE -fsanitize=fuzzer)
    # Short bounded pass so it can run on every build; local deep fuzzing just
    # runs the binary without -max_total_time.
    add_test(NAME sfz_fuzz_short
        COMMAND sfz-fuzzer -max_total_time=30 -timeout=10 "${FBSAMPLER_FUZZ_CORPUS_DIR}")
else()
    add_executable(sfz-fuzz-replay
        fuzz/sfz/sfz_fuzzer.cpp
        fuzz/sfz/replay_main.cpp
    )
    target_link_libraries(sfz-fuzz-replay PRIVATE sampler-core)
    target_compile_definitions(sfz-fuzz-replay
        PRIVATE FBSAMPLER_FUZZ_CORPUS_DIR="${FBSAMPLER_FUZZ_CORPUS_DIR}")
    add_test(NAME sfz_fuzz_replay COMMAND sfz-fuzz-replay)
    # A parser hang must fail this one test, not stall the whole CI leg until
    # the job-level timeout (the libFuzzer variant gets this via -timeout=10).
    set_tests_properties(sfz_fuzz_replay PROPERTIES TIMEOUT 120)
endif()

# AD-6 guard negative test: proves fbsampler_assert_no_gui_modules() actually
# fires by configuring an isolated fixture that links a fake GUI module and
# expecting that configure to fail.
# --fresh discards any stale CMakeCache.txt from a previous run so the fixture
# configure is reproducible between invocations.
add_test(NAME ad6_guard_negative
    COMMAND ${CMAKE_COMMAND} --fresh
        -S "${CMAKE_CURRENT_SOURCE_DIR}/ad6_guard_negative"
        -B "${CMAKE_CURRENT_BINARY_DIR}/ad6_guard_negative_build"
)
set_tests_properties(ad6_guard_negative PROPERTIES
    PASS_REGULAR_EXPRESSION "AD-6 violation"
)
