#pragma once

#include <Windows.h>
#include <evntrace.h>
#include <string>
#include <functional>
#include <atomic>

class EtwMonitor {
public:
    using FpsCallback = std::function<void(DWORD pid, double fps, double frameTimeMs)>;
    
    EtwMonitor();
    ~EtwMonitor();
    
    bool Start(DWORD targetPid, FpsCallback callback);
    void Stop();
    bool IsRunning() const { return m_running; }
    
    double GetCurrentFps() const { return m_currentFps; }
    double GetFrameTimeMs() const { return m_frameTimeMs; }
    
private:
    static DWORD WINAPI TraceThread(LPVOID param);
    void ProcessTrace();
    
    DWORD m_targetPid = 0;
    FpsCallback m_callback;
    std::atomic<bool> m_running{false};
    HANDLE m_traceThread = nullptr;
    TRACEHANDLE m_sessionHandle = 0;
    TRACEHANDLE m_traceHandle = 0;
    
    std::atomic<double> m_currentFps{0.0};
    std::atomic<double> m_frameTimeMs{0.0};
};
