#include <catch2/catch_test_macros.hpp>

#include "fbsampler/version.h"

#include <string>

TEST_CASE("core reports a semantic version", "[version]")
{
    const std::string version = fbsampler::coreVersion();
    REQUIRE_FALSE(version.empty());
    REQUIRE(version == "0.1.0");
}
