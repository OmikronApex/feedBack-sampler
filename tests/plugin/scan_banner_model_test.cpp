// Story 3.6 AC1: scan banner view-model — fake scan feed, no UI.
// Text shape + ~4 Hz notify throttle are pure decisions.

#include "ui/scan_banner_model.h"

#include <catch2/catch_test_macros.hpp>

using fbsampler::ScanProgress;
using fbsampler::ui::ScanBannerModel;

namespace {

ScanProgress tick(const char* path, std::size_t recognized)
{
    ScanProgress p;
    p.currentPath = path;
    p.recognized = recognized;
    return p;
}

} // namespace

TEST_CASE("banner text names current path and recognized count",
          "[scanbanner]")
{
    ScanBannerModel model;
    model.onProgress(tick("C:/libraries/vcsl/claves.sfz", 3), 0);
    CHECK(model.text == "C:/libraries/vcsl/claves.sfz  (3 libraries)");

    model.onProgress(tick("C:/libraries/gm.sf2", 4), 1000);
    CHECK(model.text == "C:/libraries/gm.sf2  (4 libraries)");
}

TEST_CASE("notify throttles to ~4 Hz but text always tracks the feed",
          "[scanbanner]")
{
    ScanBannerModel model;

    // First tick notifies immediately.
    CHECK(model.onProgress(tick("a", 1), 1000));

    // Ticks within 250 ms update text without notifying.
    CHECK_FALSE(model.onProgress(tick("b", 2), 1100));
    CHECK_FALSE(model.onProgress(tick("c", 3), 1249));
    CHECK(model.text == "c  (3 libraries)");

    // 250 ms elapsed since last notify: fire again.
    CHECK(model.onProgress(tick("d", 4), 1250));
    CHECK_FALSE(model.onProgress(tick("e", 5), 1400));
    CHECK(model.onProgress(tick("f", 6), 1600));
}
