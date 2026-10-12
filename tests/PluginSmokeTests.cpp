#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#pragma warning(push)
#pragma warning(disable: 4100)
#include "../sdk/TVTestPlugin.h"
#pragma warning(pop)

static TVTest::EventCallbackFunc eventCallback = nullptr;
static void *eventContext = nullptr;
static TVTest::WindowMessageCallbackFunc windowCallback = nullptr;
static bool rejectWindowCallback = false;
static int errors = 0;
static int registeredButtons = 0;
static void CheckAt(bool condition, int line) {
    if (!condition) { std::cerr << "FAILED at line " << line << " (Win32 " << GetLastError() << ")\n"; std::exit(1); }
}
#define Check(condition) CheckAt(!!(condition), __LINE__)
static LRESULT CALLBACK Host(TVTest::PluginParam *, TVTest::MessageCode message, LPARAM p1, LPARAM p2) {
    switch (message) {
    case TVTest::MESSAGE_REGISTERCONTROLLER: {
        const auto *info = reinterpret_cast<const TVTest::ControllerInfo *>(p1);
        registeredButtons = info->NumButtons;
        Check(info->Size == sizeof(*info) && registeredButtons == 767);
        Check(std::wstring(info->pButtonList[0x223 - 1].pszName) == L"AC Home");
        Check(std::wstring(info->pButtonList[0xE2 - 1].pszName) == L"Mute");
        Check(std::wstring(info->pButtonList[0x2FF - 1].pszName) == L"Usage 0x02FF");
        for (int i = 0; i < registeredButtons; ++i)
            Check(info->pButtonList[i].pszDefaultCommand == nullptr);
        return TRUE;
    }
    case TVTest::MESSAGE_SETEVENTCALLBACK:
        eventCallback = reinterpret_cast<TVTest::EventCallbackFunc>(p1);
        eventContext = reinterpret_cast<void *>(p2);
        return TRUE;
    case TVTest::MESSAGE_SETWINDOWMESSAGECALLBACK:
        if (p1 && rejectWindowCallback) return FALSE;
        windowCallback = reinterpret_cast<TVTest::WindowMessageCallbackFunc>(p1);
        return TRUE;
    case TVTest::MESSAGE_ADDLOG:
        if (p2 == TVTest::LOG_TYPE_ERROR) ++errors;
        std::wcout << reinterpret_cast<const wchar_t *>(p1) << L'\n';
        return TRUE;
    default: return 0;
    }
}
static std::vector<RAWINPUTDEVICE> Registrations() {
    UINT count = 0;
    Check(GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) != UINT(-1));
    std::vector<RAWINPUTDEVICE> list(count);
    if (count) Check(GetRegisteredRawInputDevices(list.data(), &count, sizeof(RAWINPUTDEVICE)) != UINT(-1));
    return list;
}
int wmain(int argc, wchar_t **argv) {
    Check(argc == 2);
    HWND window = CreateWindowExW(0, L"STATIC", L"HIDRemocon Test", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(window != nullptr);
    // Emulate TVTest's existing registration, which must survive the plugin.
    RAWINPUTDEVICE original{0x0C, 1, RIDEV_INPUTSINK, window};
    Check(RegisterRawInputDevices(&original, 1, sizeof(original)) != FALSE);
    HMODULE module = LoadLibraryW(argv[1]);
    Check(module != nullptr);
    auto initialize = reinterpret_cast<BOOL (WINAPI *)(TVTest::PluginParam *)>(GetProcAddress(module, "TVTInitialize"));
    auto finalize = reinterpret_cast<BOOL (WINAPI *)()>(GetProcAddress(module, "TVTFinalize"));
    Check(initialize && finalize && GetProcAddress(module, "TVTGetVersion") && GetProcAddress(module, "TVTGetPluginInfo"));
    TVTest::PluginParam param{Host, window, nullptr, nullptr};
    Check(initialize(&param) && registeredButtons == 767 && eventCallback);
    Check(eventCallback(TVTest::EVENT_PLUGINENABLE, TRUE, 0, eventContext) != FALSE);
    Check(windowCallback && Registrations().size() == 2);
    Check(eventCallback(TVTest::EVENT_PLUGINENABLE, FALSE, 0, eventContext) != FALSE);
    Check(!windowCallback && Registrations().size() == 1);
    Check(Registrations()[0].usUsage == 1 && Registrations()[0].hwndTarget == window);
    Check(errors == 0);
    // Failure after page registration must roll it back.
    rejectWindowCallback = true;
    Check(eventCallback(TVTest::EVENT_PLUGINENABLE, TRUE, 0, eventContext) == FALSE);
    Check(Registrations().size() == 1 && errors == 1);
    rejectWindowCallback = false;
    // A conflicting Consumer registration must be rejected without overwriting it.
    HWND other = CreateWindowExW(0, L"STATIC", L"Other", 0, 0, 0, 0, 0, HWND_MESSAGE,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    original.hwndTarget = other;
    Check(RegisterRawInputDevices(&original, 1, sizeof(original)) != FALSE);
    Check(eventCallback(TVTest::EVENT_PLUGINENABLE, TRUE, 0, eventContext) == FALSE);
    Check(Registrations().size() == 1 && Registrations()[0].hwndTarget == other && errors == 2);
    original.hwndTarget = window;
    Check(RegisterRawInputDevices(&original, 1, sizeof(original)) != FALSE);
    Check(eventCallback(TVTest::EVENT_PLUGINENABLE, TRUE, 0, eventContext) != FALSE);
    Check(finalize()); // unloading while enabled must also clean up
    Check(!windowCallback && Registrations().size() == 1);
    FreeLibrary(module);
    RAWINPUTDEVICE remove{0x0C, 1, RIDEV_REMOVE, nullptr};
    Check(RegisterRawInputDevices(&remove, 1, sizeof(remove)) != FALSE);
    DestroyWindow(other);
    DestroyWindow(window);
    std::cout << "Plugin ABI, controller list, enable/disable, rollback, conflict and unload checks passed\n";
}
