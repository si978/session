#pragma once
#include <Windows.h>
#include <fstream>
#include <cstdarg>
#include <ctime>

namespace Logger {
    inline std::ofstream g_file;
    inline bool g_initialized = false;

    inline void Initialize(const char* filename = "fps_overlay.log") {
        if (g_initialized) return;
        
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        
        std::string fullPath(path);
        size_t lastSlash = fullPath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            fullPath = fullPath.substr(0, lastSlash + 1);
        }
        fullPath += filename;
        
        g_file.open(fullPath, std::ios::app);
        g_initialized = true;
    }

    inline void Log(const char* format, ...) {
        time_t now = time(nullptr);
        char timeStr[32];
        strftime(timeStr, sizeof(timeStr), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
        
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        
        OutputDebugStringA(timeStr);
        OutputDebugStringA(" ");
        OutputDebugStringA(buffer);
        OutputDebugStringA("\n");
        
        if (g_file.is_open()) {
            g_file << timeStr << " " << buffer << std::endl;
            g_file.flush();
        }
    }

    inline void Shutdown() {
        if (g_file.is_open()) {
            g_file.close();
        }
        g_initialized = false;
    }
}

#define LOG(fmt, ...) Logger::Log("[FPS] " fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Logger::Log("[FPS ERROR] " fmt, ##__VA_ARGS__)
