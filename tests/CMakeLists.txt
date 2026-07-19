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

# --- Offline render harness + render-regression + RT-safety (Story 1.4) ---
# Separate binary: it carries global operator new/delete overrides for the
# RT-safety detector, which must not leak into the main unit-test binary.
add_executable(fbsampler-render-tests
    render/wav_io.cpp
    render/render_harness.cpp
    render/render_regression_test.cpp
    render/rt_safety_test.cpp
    render/rt_new_delete_hooks.cpp
    render/model_to_sfz_test.cpp
    render/pool_test.cpp
    render/midi_behavior_test.cpp
    render/midi_file.cpp
    render/midi_file_test.cpp
    render/corpus_render.cpp
    render/corpus_render_test.cpp
)

target_link_libraries(fbsampler-render-tests
    PRIVATE
        sampler-core
        Catch2::Catch2WithMain
)

# Internal-seam tests (model_to_sfz, wav_reader) reach into core/ directly.
target_include_directories(fbsampler-render-tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/core"
)

target_compile_definitions(fbsampler-render-tests
    PRIVATE
        FBSAMPLER_RENDER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/render/fixtures"
)

catch_discover_tests(fbsampler-render-tests)

# --- Corpus entry renderer (Story 1.6) ---
# One-entry CLI over the same corpus_render library code the render-tests pin.
# corpus/tools/run_corpus.py orchestrates it across the manifest; CI's corpus
# job is the only consumer that needs network (cached corpus assets).
add_executable(fbsampler-corpus-render
    render/corpus_render_main.cpp
    render/corpus_render.cpp
    render/midi_file.cpp
    render/render_harness.cpp
    render/wav_io.cpp
)

target_link_libraries(fbsampler-corpus-render PRIVATE sampler-core)

target_include_directories(fbsampler-corpus-render
    PRIVATE
        "${CMAKE_SOURCE_DIR}/core"
)

# --- Plugin shell tests (Story 1.5) ---
# Headless processBlock through the real PluginProcessor: MIDI translation,
# load path, RT-detector clean. Own binary: carries the global new/delete
# RT hooks and the JUCE runtime.
juce_add_console_app(fbsampler-plugin-tests PRODUCT_NAME "fbsampler-plugin-tests")

target_sources(fbsampler-plugin-tests
    PRIVATE
        plugin/plugin_processor_test.cpp
        ${CMAKE_SOURCE_DIR}/plugin/plugin_processor.cpp
        ${CMAKE_SOURCE_DIR}/plugin/plugin_editor.cpp
        render/rt_new_delete_hooks.cpp
)

target_include_directories(fbsampler-plugin-tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/plugin"
)

target_compile_definitions(fbsampler-plugin-tests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        FBSAMPLER_NO_PLUGIN_FILTER_ENTRY=1
        FBSAMPLER_RENDER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/render/fixtures"
)

target_link_libraries(fbsampler-plugin-tests
    PRIVATE
        sampler-core
        Catch2::Catch2WithMain
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

catch_discover_tests(fbsampler-plugin-tests)

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
