#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <string>

bool EnableDebugPrivilege() {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }
    
    LUID luid;
    if (!LookupPrivilegeValue(nullptr, SE_DEBUG_NAME, &luid)) {
        CloseHandle(hToken);
        return false;
    }
    
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr)) {
        CloseHandle(hToken);
        return false;
    }
    
    CloseHandle(hToken);
    return GetLastError() != ERROR_NOT_ALL_ASSIGNED;
}

DWORD GetProcessIdByName(const wchar_t* processName) {
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        
        if (Process32FirstW(snapshot, &pe32)) {
            do {
                if (_wcsicmp(pe32.szExeFile, processName) == 0) {
                    pid = pe32.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &pe32));
        }
        CloseHandle(snapshot);
    }
    
    return pid;
}

bool InjectDll(DWORD pid, const wchar_t* dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    
    if (!hProcess) {
        std::wcerr << L"[ERROR] Failed to open process. Error: " << GetLastError() << std::endl;
        return false;
    }
    
    size_t dllPathSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    LPVOID pRemoteMem = VirtualAllocEx(
        hProcess, nullptr, dllPathSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE
    );
    
    if (!pRemoteMem) {
        std::wcerr << L"[ERROR] Failed to allocate memory. Error: " << GetLastError() << std::endl;
        CloseHandle(hProcess);
        return false;
    }
    
    if (!WriteProcessMemory(hProcess, pRemoteMem, dllPath, dllPathSize, nullptr)) {
        std::wcerr << L"[ERROR] Failed to write memory. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    LPVOID pLoadLibrary = (LPVOID)GetProcAddress(hKernel32, "LoadLibraryW");
    
    if (!pLoadLibrary) {
        std::wcerr << L"[ERROR] Failed to get LoadLibraryW address." << std::endl;
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    
    HANDLE hThread = CreateRemoteThread(
        hProcess, nullptr, 0,
        (LPTHREAD_START_ROUTINE)pLoadLibrary,
        pRemoteMem, 0, nullptr
    );
    
    if (!hThread) {
        std::wcerr << L"[ERROR] Failed to create remote thread. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    
    WaitForSingleObject(hThread, INFINITE);
    
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    
    if (exitCode == 0) {
        std::wcerr << L"[ERROR] LoadLibrary failed in target process." << std::endl;
        return false;
    }
    
    return true;
}

std::wstring GetDllPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    
    std::wstring path(exePath);
    size_t lastSlash = path.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        path = path.substr(0, lastSlash + 1);
    }
    
    return path + L"fps_overlay.dll";
}

void PrintUsage(const wchar_t* exeName) {
    std::wcout << L"\n";
    std::wcout << L"========================================\n";
    std::wcout << L"       FPS Overlay Injector\n";
    std::wcout << L"========================================\n";
    std::wcout << L"\n";
    std::wcout << L"Usage:\n";
    std::wcout << L"  " << exeName << L" <process_name>\n";
    std::wcout << L"  " << exeName << L" <process_id>\n";
    std::wcout << L"\n";
    std::wcout << L"Examples:\n";
    std::wcout << L"  " << exeName << L" Game.exe\n";
    std::wcout << L"  " << exeName << L" 12345\n";
    std::wcout << L"\n";
    std::wcout << L"Note: Run as Administrator!\n";
    std::wcout << L"\n";
}

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    
    if (!EnableDebugPrivilege()) {
        std::wcerr << L"[WARN] Failed to enable debug privilege. Try running as Administrator." << std::endl;
    }
    
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }
    
    DWORD pid = 0;
    
    if (iswdigit(argv[1][0])) {
        pid = _wtoi(argv[1]);
    } else {
        std::wcout << L"[INFO] Searching for process: " << argv[1] << std::endl;
        pid = GetProcessIdByName(argv[1]);
    }
    
    if (pid == 0) {
        std::wcerr << L"[ERROR] Process not found!" << std::endl;
        return 1;
    }
    
    std::wcout << L"[INFO] Target PID: " << pid << std::endl;
    
    std::wstring dllPath = GetDllPath();
    std::wcout << L"[INFO] DLL Path: " << dllPath << std::endl;
    
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wcerr << L"[ERROR] DLL not found: " << dllPath << std::endl;
        return 1;
    }
    
    std::wcout << L"[INFO] Injecting..." << std::endl;
    
    if (InjectDll(pid, dllPath.c_str())) {
        std::wcout << L"[SUCCESS] DLL injected successfully!" << std::endl;
        std::wcout << L"[INFO] Press F1 in game to toggle overlay." << std::endl;
        return 0;
    } else {
        std::wcerr << L"[ERROR] Injection failed!" << std::endl;
        return 1;
    }
}
