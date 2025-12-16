#pragma once

namespace FpsCounter {
    void Update();
    float GetFps();
    float GetFrameTime();
    float GetDisplayFps();     // For display (smoothed, updated periodically)
    float GetDisplayFrameTime();
}
