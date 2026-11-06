// Copyright Ethan Porcaro; maintenance changes 2026. GPL-3.0-or-later.
#include <Windows.h>
#include <TlHelp32.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
namespace fs = std::filesystem;
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
};
void Error(const char* text) { std::cerr << text << " (Windows error " << GetLastError() << ")\n"; }
fs::path ExecutablePath() {
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
}
bool AlreadyLoaded(DWORD pid) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (snapshot.value == INVALID_HANDLE_VALUE) { Error("Cannot inspect modules"); return true; }
    MODULEENTRY32W module{}; module.dwSize = sizeof(module);
    if (!Module32FirstW(snapshot.value, &module)) { Error("Cannot enumerate modules"); return true; }
    do { if (_wcsicmp(module.szModule, L"BF2VR.dll") == 0) return true; }
    while (Module32NextW(snapshot.value, &module));
    return false;
}
uintptr_t RemoteLoadLibrary(DWORD pid) {
    auto local = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = nullptr;
    if (!local || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(local), &owner)) return 0;
    wchar_t name[32768]{};
    if (!GetModuleFileNameW(owner, name, 32768)) return 0;
    auto filename = fs::path(name).filename().wstring();
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (snapshot.value == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W module{}; module.dwSize = sizeof(module);
    if (!Module32FirstW(snapshot.value, &module)) return 0;
    do {
        if (_wcsicmp(module.szModule, filename.c_str()) == 0)
            return reinterpret_cast<uintptr_t>(module.modBaseAddr) + reinterpret_cast<uintptr_t>(local) - reinterpret_cast<uintptr_t>(owner);
    } while (Module32NextW(snapshot.value, &module));
    return 0;
}
int Inject(DWORD pid, const fs::path& dll) {
    Handle process(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid));
    if (!process.value) { Error("Cannot open game"); return 1; }
    BOOL wow64 = FALSE;
    if (!IsWow64Process(process.value, &wow64) || wow64) { std::cerr << "Native x64 game required\n"; return 1; }
    if (AlreadyLoaded(pid)) { std::cerr << "BF2VR already loaded or modules unavailable. Restart the game before retrying.\n"; return 1; }
    auto loadLibrary = RemoteLoadLibrary(pid);
    if (!loadLibrary) { Error("Cannot resolve remote LoadLibraryW"); return 1; }
    auto path = dll.wstring();
    SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process.value, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { Error("Cannot allocate DLL path"); return 1; }
    SIZE_T written = 0;
    if (!WriteProcessMemory(process.value, remote, path.c_str(), bytes, &written) || written != bytes) {
        Error("Cannot write DLL path"); VirtualFreeEx(process.value, remote, 0, MEM_RELEASE); return 1;
    }
    Handle thread(CreateRemoteThread(process.value, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary), remote, 0, nullptr));
    if (!thread.value) { Error("Cannot create loader thread"); VirtualFreeEx(process.value, remote, 0, MEM_RELEASE); return 1; }
    if (WaitForSingleObject(thread.value, 15000) != WAIT_OBJECT_0) {
        // A live remote thread may still read this allocation. The game owns it until exit.
        std::cerr << "DLL loading timed out. Restart the game before retrying.\n"; return 1;
    }
    DWORD result = 0;
    bool loaded = GetExitCodeThread(thread.value, &result) && result != 0;
    VirtualFreeEx(process.value, remote, 0, MEM_RELEASE);
    if (!loaded) { std::cerr << "LoadLibraryW failed. Check DLL dependencies and Documents/BF2VR/BF2VR.txt.\n"; return 1; }
    std::cout << "DLL loaded into PID " << pid << ". VR startup is reported in the console and log.\n";
    return 0;
}
int wmain(int argc, wchar_t** argv) {
    auto executable = ExecutablePath();
    auto dll = executable.parent_path() / L"BF2VR.dll";
    if (argc == 2 && wcscmp(argv[1], L"--check") == 0) {
        if (!fs::is_regular_file(dll)) { std::cerr << "BF2VR.dll missing beside Loader.exe\n"; return 1; }
        HMODULE module = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
        if (!module) { Error("Cannot inspect DLL image"); return 1; }
        FreeLibrary(module);
        std::wcout << L"DLL image found: " << dll.wstring() << L"\nNo injection performed.\n";
        return 0;
    }
    if (argc > 1) { std::cout << "Usage: Loader.exe [--check]\n"; return 1; }
    if (executable.empty() || !fs::is_regular_file(dll)) { std::cerr << "BF2VR.dll must be beside Loader.exe\n"; return 1; }
    std::cout << "BF2VR improved - offline Arcade only.\nStart an Arcade match with DX12 off, TAA high, and borderless mode.\nPress ENTER once the match and OpenXR runtime are ready, or close this window to cancel.\n";
    std::cin.get();
    HWND window = FindWindowW(L"Frostbite", L"STAR WARS Battlefront II");
    DWORD pid = 0; if (window) GetWindowThreadProcessId(window, &pid);
    if (!pid) { std::cerr << "Battlefront II window not found\n"; return 1; }
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    wchar_t image[32768]{}; DWORD size = 32768;
    if (!process.value || !QueryFullProcessImageNameW(process.value, 0, image, &size) ||
        _wcsicmp(fs::path(image).filename().c_str(), L"starwarsbattlefrontii.exe") != 0) {
        std::cerr << "Window does not belong to expected game executable\n"; return 1;
    }
    int result = Inject(pid, dll);
    if (result) { std::cout << "Press ENTER to close.\n"; std::cin.get(); }
    return result;
}
