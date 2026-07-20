#pragma once

// Story 3.6: pure scan-banner view-model — decides the banner text and the
// ~4 Hz notify throttle without any UI or clock dependency. The processor's
// scan callback feeds it; components only render `text`.

#include "fbsampler/config_service.h"

#include <cstdint>
#include <string>

namespace fbsampler::ui {

struct ScanBannerModel {
    std::string text;

    /// Feed one scan progress tick at time `nowMs` (any monotonic ms clock).
    /// Always updates `text`; returns true when a change notification should
    /// fire (first tick, then at most every 250 ms).
    bool onProgress(const ScanProgress& p, std::int64_t nowMs)
    {
        text = p.currentPath + "  ("
               + std::to_string(p.recognized) + " libraries)";
        if (notified_ && nowMs - lastNotifyMs_ < 250)
            return false;
        notified_ = true;
        lastNotifyMs_ = nowMs;
        return true;
    }

private:
    bool notified_ = false;
    std::int64_t lastNotifyMs_ = 0;
};

} // namespace fbsampler::ui
