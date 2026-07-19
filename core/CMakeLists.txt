# Generated from config/version.cpp.in so coreVersion() always tracks the
# top-level project(VERSION ...) instead of drifting as an independent literal.
configure_file(
    config/version.cpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
    @ONLY
)

add_library(sampler-core STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
    model/validate.cpp
    model/serialize.cpp
    frontends/sfz/sfz_frontend.cpp
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
        sfizz::parser
)

set_target_properties(sampler-core PROPERTIES
    OUTPUT_NAME sampler-core
)
