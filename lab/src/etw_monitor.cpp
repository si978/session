#include "etw_monitor.h"
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <deque>
#include <mutex>

#pragma comment(lib, "tdh.lib")

// Microsoft-Windows-DXGI GUID
static const GUID DXGI_PROVIDER = 
    { 0xCA11C036, 0x0102, 0x4A2D, { 0xA6, 0xAD, 0xF0, 0x3C, 0xFD, 0xAC, 0xC5, 0x17 } };

// Present event ID
static const USHORT DXGI_PRESENT_EVENT = 42;

// Global instance for callback
static EtwMonitor* g_instance = nullptr;
static std::mutex g_frameMutex;
static std::deque<LARGE_INTEGER> g_frameTimes;
static LARGE_INTEGER g_frequency;
static DWORD g_targetPid = 0;

static void WINAPI EventRecordCallback(PEVENT_RECORD pEvent) {
    if (!g_instance || !pEvent) return;
    
    // Check if it's from our target process
    if (pEvent->EventHeader.ProcessId != g_targetPid) return;
    
    // Check if it's a DXGI Present event
    if (!IsEqualGUID(pEvent->EventHeader.ProviderId, DXGI_PROVIDER)) return;
    if (pEvent->EventHeader.EventDescriptor.Id != DXGI_PRESENT_EVENT) return;
    
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    
    std::lock_guard<std::mutex> lock(g_frameMutex);
    g_frameTimes.push_back(now);
    
    // Keep only last 2 seconds of frames
    double twoSecondsAgo = (double)(now.QuadPart - 2 * g_frequency.QuadPart);
    while (!g_frameTimes.empty() && 
           (double)g_frameTimes.front().QuadPart < twoSecondsAgo) {
        g_frameTimes.pop_front();
    }
}

EtwMonitor::EtwMonitor() {
    QueryPerformanceFrequency(&g_frequency);
}

EtwMonitor::~EtwMonitor() {
    Stop();
}

bool EtwMonitor::Start(DWORD targetPid, FpsCallback callback) {
    if (m_running) return false;
    
    m_targetPid = targetPid;
    m_callback = callback;
    g_targetPid = targetPid;
    g_instance = this;
    
    m_running = true;
    m_traceThread = CreateThread(nullptr, 0, TraceThread, this, 0, nullptr);
    
    return m_traceThread != nullptr;
}

void EtwMonitor::Stop() {
    m_running = false;
    
    if (m_traceHandle != 0) {
        CloseTrace(m_traceHandle);
        m_traceHandle = 0;
    }
    
    if (m_sessionHandle != 0) {
        EVENT_TRACE_PROPERTIES* props = (EVENT_TRACE_PROPERTIES*)malloc(
            sizeof(EVENT_TRACE_PROPERTIES) + 256);
        ZeroMemory(props, sizeof(EVENT_TRACE_PROPERTIES) + 256);
        props->Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES) + 256;
        
        ControlTraceW(m_sessionHandle, nullptr, props, EVENT_TRACE_CONTROL_STOP);
        free(props);
        m_sessionHandle = 0;
    }
    
    if (m_traceThread) {
        WaitForSingleObject(m_traceThread, 3000);
        CloseHandle(m_traceThread);
        m_traceThread = nullptr;
    }
    
    g_instance = nullptr;
    g_frameTimes.clear();
}

DWORD WINAPI EtwMonitor::TraceThread(LPVOID param) {
    EtwMonitor* self = (EtwMonitor*)param;
    self->ProcessTrace();
    return 0;
}

void EtwMonitor::ProcessTrace() {
    const wchar_t* sessionName = L"FPSMonitorSession";
    
    // Stop any existing session with same name
    size_t bufferSize = sizeof(EVENT_TRACE_PROPERTIES) + (wcslen(sessionName) + 1) * sizeof(wchar_t) + 256;
    EVENT_TRACE_PROPERTIES* sessionProps = (EVENT_TRACE_PROPERTIES*)malloc(bufferSize);
    ZeroMemory(sessionProps, bufferSize);
    
    sessionProps->Wnode.BufferSize = (ULONG)bufferSize;
    sessionProps->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    sessionProps->Wnode.ClientContext = 1;
    sessionProps->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    sessionProps->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    
    ControlTraceW(0, sessionName, sessionProps, EVENT_TRACE_CONTROL_STOP);
    
    // Start new session
    ZeroMemory(sessionProps, bufferSize);
    sessionProps->Wnode.BufferSize = (ULONG)bufferSize;
    sessionProps->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    sessionProps->Wnode.ClientContext = 1;
    sessionProps->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    sessionProps->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    
    ULONG status = StartTraceW(&m_sessionHandle, sessionName, sessionProps);
    if (status != ERROR_SUCCESS) {
        free(sessionProps);
        m_running = false;
        return;
    }
    
    // Enable DXGI provider
    status = EnableTraceEx2(m_sessionHandle, &DXGI_PROVIDER,
                            EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                            TRACE_LEVEL_INFORMATION, 0, 0, 0, nullptr);
    
    if (status != ERROR_SUCCESS) {
        ControlTraceW(m_sessionHandle, nullptr, sessionProps, EVENT_TRACE_CONTROL_STOP);
        free(sessionProps);
        m_running = false;
        return;
    }
    
    // Open trace for processing
    EVENT_TRACE_LOGFILEW logfile = {0};
    logfile.LoggerName = (LPWSTR)sessionName;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = EventRecordCallback;
    
    m_traceHandle = OpenTraceW(&logfile);
    if (m_traceHandle == INVALID_PROCESSTRACE_HANDLE) {
        ControlTraceW(m_sessionHandle, nullptr, sessionProps, EVENT_TRACE_CONTROL_STOP);
        free(sessionProps);
        m_running = false;
        return;
    }
    
    // Start a separate thread for FPS calculation
    HANDLE calcThread = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        EtwMonitor* self = (EtwMonitor*)p;
        
        while (self->m_running) {
            Sleep(100);
            
            std::lock_guard<std::mutex> lock(g_frameMutex);
            
            if (g_frameTimes.size() >= 2) {
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                
                // Count frames in last second
                double oneSecondAgo = (double)(now.QuadPart - g_frequency.QuadPart);
                int frameCount = 0;
                double lastFrameTime = 0;
                
                for (auto it = g_frameTimes.rbegin(); it != g_frameTimes.rend(); ++it) {
                    if ((double)it->QuadPart >= oneSecondAgo) {
                        frameCount++;
                        if (it != g_frameTimes.rbegin()) {
                            auto prev = it;
                            --prev;
                            lastFrameTime = (double)(prev->QuadPart - it->QuadPart) / g_frequency.QuadPart * 1000.0;
                        }
                    }
                }
                
                double fps = (double)frameCount;
                self->m_currentFps = fps;
                self->m_frameTimeMs = lastFrameTime;
                
                if (self->m_callback) {
                    self->m_callback(self->m_targetPid, fps, lastFrameTime);
                }
            }
        }
        return 0;
    }, this, 0, nullptr);
    
    // Process trace (this blocks until trace is closed)
    ::ProcessTrace(&m_traceHandle, 1, nullptr, nullptr);
    
    // Cleanup
    WaitForSingleObject(calcThread, 1000);
    CloseHandle(calcThread);
    
    ControlTraceW(m_sessionHandle, nullptr, sessionProps, EVENT_TRACE_CONTROL_STOP);
    free(sessionProps);
}
