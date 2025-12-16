#pragma once

namespace FpsCounter {
    void Update();
    float GetFps();
    float GetFrameTime();
    int GetDisplayFps();       // For display (updated every 300ms)
    float GetDisplayFrameTime();
}
