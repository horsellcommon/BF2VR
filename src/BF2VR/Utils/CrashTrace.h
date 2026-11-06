#pragma once
#include <Windows.h>
#include <ShlObj.h>
#include <cstdio>

namespace BF2VR {
// First-chance records are diagnostic only: the game/runtime may handle them.
// Avoid iostreams, heap allocations and application mutexes in this callback.
struct CrashTrace {
    static inline HANDLE file = INVALID_HANDLE_VALUE;
    static inline PVOID handler = nullptr;
    static inline LONG count = 0;
    static LONG CALLBACK Record(EXCEPTION_POINTERS* exception) {
        const DWORD code = exception->ExceptionRecord->ExceptionCode;
        if (file == INVALID_HANDLE_VALUE || (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_IN_PAGE_ERROR)) return EXCEPTION_CONTINUE_SEARCH;
        if (InterlockedIncrement(&count) > 16) return EXCEPTION_CONTINUE_SEARCH;
        char text[1024]{};
        DWORD written = 0;
        HMODULE owner = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(exception->ExceptionRecord->ExceptionAddress), &owner);
        char module[MAX_PATH]{};
        if (owner) GetModuleFileNameA(owner, module, MAX_PATH);
        int length = _snprintf_s(text, sizeof(text), _TRUNCATE,
            "First-chance exception (may be handled): code=0x%08lX thread=%lu address=%p module=%s offset=0x%llX\r\n",
            code, GetCurrentThreadId(), exception->ExceptionRecord->ExceptionAddress, module,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress) - reinterpret_cast<uintptr_t>(owner)));
        if (length > 0) WriteFile(file, text, length, &written, nullptr);
        if (code == EXCEPTION_ACCESS_VIOLATION && exception->ExceptionRecord->NumberParameters >= 2) {
            length = _snprintf_s(text, sizeof(text), _TRUNCATE, "Access operation=%llu target=%p\r\n",
                static_cast<unsigned long long>(exception->ExceptionRecord->ExceptionInformation[0]),
                reinterpret_cast<void*>(exception->ExceptionRecord->ExceptionInformation[1]));
            if (length > 0) WriteFile(file, text, length, &written, nullptr);
        }
        void* frames[24]{};
        USHORT captured = CaptureStackBackTrace(0, 24, frames, nullptr);
        for (USHORT i = 0; i < captured; ++i) {
            owner = nullptr; module[0] = '\0';
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(frames[i]), &owner);
            if (owner) GetModuleFileNameA(owner, module, MAX_PATH);
            length = _snprintf_s(text, sizeof(text), _TRUNCATE, "  frame=%u address=%p module=%s offset=0x%llX\r\n", i, frames[i], module,
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(owner)));
            if (length > 0) WriteFile(file, text, length, &written, nullptr);
        }
        FlushFileBuffers(file);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    static void Start() {
        wchar_t documents[MAX_PATH]{};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents))) return;
        wchar_t path[MAX_PATH + 80]{};
        _snwprintf_s(path, _countof(path), _TRUNCATE, L"%s\\BF2VR\\NativeTrace-%lu.txt", documents, GetCurrentProcessId());
        file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) handler = AddVectoredExceptionHandler(1, Record);
    }
    static void Stop() {
        // The module and file handle remain resident to avoid racing an active
        // first-chance callback; the OS releases the handle at process exit.
        if (handler) RemoveVectoredExceptionHandler(handler);
        handler = nullptr;
    }
};
}
