add_library(sampler-core STATIC
    config/version.cpp
)

target_include_directories(sampler-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# sfizz is an internal engine dependency; the public API speaks only
# fbsampler types + std, so everything stays PRIVATE.
target_link_libraries(sampler-core
    PRIVATE
        sfizz::sfizz
)

set_target_properties(sampler-core PROPERTIES
    OUTPUT_NAME sampler-core
)
