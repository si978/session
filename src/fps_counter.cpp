#include "fps_counter.h"
#include <Windows.h>
#include <chrono>
#include <deque>

namespace FpsCounter {
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    
    static TimePoint s_lastFrameTime;
    static TimePoint s_lastDisplayUpdate;
    static float s_fps = 0.0f;
    static float s_frameTime = 0.0f;
    static float s_displayFps = 0.0f;
    static float s_displayFrameTime = 0.0f;
    static std::deque<float> s_frameTimes;
    static size_t s_sampleCount = 60;
    static long long s_displayUpdateMs = 80;
    static bool s_firstFrame = true;
    static bool s_firstDisplay = true;

    void SetSampleCount(size_t n) {
        if (n < 1) n = 1;
        if (n > 1000) n = 1000;

        s_sampleCount = n;
        while (s_frameTimes.size() > s_sampleCount) {
            s_frameTimes.pop_front();
        }
    }

    void SetDisplayUpdateMs(long long ms) {
        if (ms < 16) ms = 16;
        if (ms > 5000) ms = 5000;
        s_displayUpdateMs = ms;
    }

    void Update() {
        TimePoint now = Clock::now();
        
        if (s_firstFrame) {
            s_lastFrameTime = now;
            s_lastDisplayUpdate = now;
            s_firstFrame = false;
            return;
        }

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - s_lastFrameTime);
        float deltaMs = duration.count() / 1000.0f;
        s_lastFrameTime = now;

        s_frameTimes.push_back(deltaMs);
        if (s_frameTimes.size() > s_sampleCount) {
            s_frameTimes.pop_front();
        }

        float totalTime = 0.0f;
        for (float t : s_frameTimes) {
            totalTime += t;
        }
        s_frameTime = totalTime / s_frameTimes.size();
        s_fps = (s_frameTime > 0.0f) ? (1000.0f / s_frameTime) : 0.0f;

        if (s_firstDisplay) {
            s_displayFps = s_fps;
            s_displayFrameTime = s_frameTime;
            s_lastDisplayUpdate = now;
            s_firstDisplay = false;
            return;
        }

        auto displayDuration = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastDisplayUpdate);
        if (displayDuration.count() >= s_displayUpdateMs) {
            s_displayFps = s_fps;
            s_displayFrameTime = s_frameTime;
            s_lastDisplayUpdate = now;
        }
    }

    float GetFps() {
        return s_fps;
    }

    float GetFrameTime() {
        return s_frameTime;
    }

    float GetDisplayFps() {
        return s_displayFps;
    }

    float GetDisplayFrameTime() {
        return s_displayFrameTime;
    }
}
