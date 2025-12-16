#pragma once
#include <Windows.h>

// Shared config structure between monitor and hook DLL
#pragma pack(push, 1)
struct FpsConfig {
    // Display
    int position;           // 0=TopLeft, 1=TopRight, 2=BottomLeft, 3=BottomRight, 4=Custom
    int offsetX;
    int offsetY;
    int fontSize;           // 12, 14, 18
    bool showBackground;
    
    // Custom position (absolute coordinates, used when position=4)
    int customX;
    int customY;
    bool positionDirty;     // Set by hook when position changed, cleared by monitor after save
    
    // Colors (RGB)
    DWORD colorHigh;        // >= 60 FPS
    DWORD colorMedium;      // 30-59 FPS
    DWORD colorLow;         // < 30 FPS
    DWORD colorBackground;  // ARGB
    
    // Hotkey
    int toggleKey;          // VK code
    bool useCtrl;
    bool useAlt;
    bool useShift;
    
    // Filter
    int filterMode;         // 0=All, 1=Whitelist, 2=Blacklist
    char gameList[4096];    // Semicolon-separated list
    
    // State
    bool visible;
    bool enabled;
};
#pragma pack(pop)

#define CONFIG_SHARED_NAME L"FpsOverlayConfig"
#define CONFIG_FILE_NAME L"fps_config.ini"

// Default config
inline void InitDefaultConfig(FpsConfig* cfg) {
    cfg->position = 0;          // TopLeft (changed from TopRight)
    cfg->offsetX = 10;
    cfg->offsetY = 10;
    cfg->fontSize = 14;
    cfg->showBackground = true;
    cfg->customX = 10;
    cfg->customY = 10;
    cfg->positionDirty = false;
    
    cfg->colorHigh = 0xFF00E070;     // Green
    cfg->colorMedium = 0xFFFFCC00;   // Yellow
    cfg->colorLow = 0xFFFF4040;      // Red
    cfg->colorBackground = 0xB0202020;
    
    cfg->toggleKey = VK_F1;
    cfg->useCtrl = false;
    cfg->useAlt = false;
    cfg->useShift = false;
    
    cfg->filterMode = 0;        // All games
    cfg->gameList[0] = '\0';
    
    cfg->visible = true;
    cfg->enabled = true;
}

// Position enum
enum FpsPosition {
    POS_TOP_LEFT = 0,
    POS_TOP_RIGHT = 1,
    POS_BOTTOM_LEFT = 2,
    POS_BOTTOM_RIGHT = 3,
    POS_CUSTOM = 4
};

// Filter mode enum
enum FilterMode {
    FILTER_ALL = 0,
    FILTER_WHITELIST = 1,
    FILTER_BLACKLIST = 2
};
