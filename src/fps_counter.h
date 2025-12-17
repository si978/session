#pragma once

#include <cstddef>

namespace FpsCounter {
    void Update();
    float GetFps();
    float GetFrameTime();
    float GetDisplayFps();     // For display (smoothed, updated periodically)
    float GetDisplayFrameTime();

    void SetSampleCount(std::size_t n);
    void SetDisplayUpdateMs(long long ms);
}
