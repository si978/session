#include <Windows.h>
#include "hooks.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
            Hooks::Initialize(static_cast<HMODULE>(param));
            return 0;
        }, hModule, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        Hooks::Shutdown();
        break;
    }
    return TRUE;
}
