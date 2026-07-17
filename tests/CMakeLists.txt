add_executable(fbsampler-tests
    version_test.cpp
)

target_link_libraries(fbsampler-tests
    PRIVATE
        sampler-core
        Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(fbsampler-tests)
