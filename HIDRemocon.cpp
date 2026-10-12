#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbt.h>
#include <hidsdi.h>
#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#define TVTEST_PLUGIN_VERSION TVTEST_PLUGIN_VERSION_(0, 0, 14)
#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#pragma warning(push)
#pragma warning(disable: 4100) // Unused parameters in SDK default event handlers.
#include "sdk/TVTestPlugin.h"
#pragma warning(pop)
#include "ConsumerUsages.h"
#include "ButtonState.h"

#pragma comment(lib, "hid.lib")

namespace {
constexpr wchar_t ControllerName[] = L"HID Consumer Control";
constexpr USAGE ConsumerPage = 0x0C;
constexpr unsigned LastUsage = 0x02FF;

struct Device {
    // DWORD storage gives the HID preparsed data its required alignment.
    std::vector<DWORD> preparsed;
    HIDP_CAPS caps{};
    std::vector<HIDP_BUTTON_CAPS> buttons;
    std::vector<HIDP_VALUE_CAPS> values;
    ButtonState state;
    PHIDP_PREPARSED_DATA Data() {
        return reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed.data());
    }
    bool Load(HANDLE handle) {
        UINT size = 0;
        if (GetRawInputDeviceInfoW(handle, RIDI_PREPARSEDDATA, nullptr, &size) == UINT(-1) || !size)
            return false;
        preparsed.resize((size + sizeof(DWORD) - 1) / sizeof(DWORD));
        if (GetRawInputDeviceInfoW(handle, RIDI_PREPARSEDDATA, preparsed.data(), &size) == UINT(-1)
                || HidP_GetCaps(Data(), &caps) != HIDP_STATUS_SUCCESS)
            return false;
        USHORT count = caps.NumberInputButtonCaps;
        buttons.resize(count);
        if (count && HidP_GetButtonCaps(HidP_Input, buttons.data(), &count, Data()) != HIDP_STATUS_SUCCESS)
            return false;
        buttons.resize(count);
        count = caps.NumberInputValueCaps;
        values.resize(count);
        if (count && HidP_GetValueCaps(HidP_Input, values.data(), &count, Data()) != HIDP_STATUS_SUCCESS)
            return false;
        values.resize(count);
        return true;
    }
    // Null reports release buttons. Arrays and bitfields are both decoded by
    // HidP_GetUsages; bytes are never interpreted as hardcoded Usage IDs.
    bool Decode(char *report, ULONG length, std::set<USHORT> &pressed) {
        if (!length || length != caps.InputReportByteLength)
            return false;
        const auto id = static_cast<UCHAR>(report[0]);
        bool recognized = false;
        if (std::any_of(buttons.begin(), buttons.end(), [id](const auto &cap) {
                return cap.UsagePage == ConsumerPage && cap.ReportID == id;
            })) {
            ULONG count = HidP_MaxUsageListLength(HidP_Input, ConsumerPage, Data());
            std::vector<USAGE> usages(std::max<ULONG>(count, 1));
            if (HidP_GetUsages(HidP_Input, ConsumerPage, 0, usages.data(), &count,
                    Data(), report, length) != HIDP_STATUS_SUCCESS)
                return false; // A malformed report must not release held buttons.
            pressed.insert(usages.begin(), usages.begin() + count);
            recognized = true;
        }
        for (const auto &cap : values) {
            // Only absolute on/off values represent buttons. Do not treat
            // volume levels, channel numbers or relative axes as button IDs.
            if (cap.UsagePage != ConsumerPage || cap.ReportID != id || !cap.IsAbsolute
                    || cap.LogicalMin != 0 || cap.LogicalMax != 1 || cap.ReportCount != 1)
                continue;
            const unsigned first = cap.IsRange ? cap.Range.UsageMin : cap.NotRange.Usage;
            const unsigned last = cap.IsRange ? cap.Range.UsageMax : first;
            for (unsigned usage = first; usage <= last; ++usage) {
                ULONG value = 0;
                if (HidP_GetUsageValue(HidP_Input, ConsumerPage, cap.LinkCollection,
                        static_cast<USAGE>(usage), &value, Data(), report, length) != HIDP_STATUS_SUCCESS)
                    return false;
                if (value) pressed.insert(static_cast<USHORT>(usage));
                recognized = true;
            }
        }
        pressed.erase(0);
        return recognized;
    }
};
}

class HIDRemocon : public TVTest::CTVTestPlugin {
    bool enabled = false;
    bool ownsPageRegistration = false;
    std::map<HANDLE, std::unique_ptr<Device>> devices;
    std::set<USHORT> warnedUsages;
    std::vector<std::wstring> names;
    std::vector<TVTest::ControllerButtonInfo> buttons;

    void Error(const wchar_t *operation) {
        wchar_t message[256];
        swprintf_s(message, L"HIDRemocon: %s (Win32 error %lu)", operation, GetLastError());
        m_pApp->AddLog(message, TVTest::LOG_TYPE_ERROR);
    }
    bool Registrations(std::vector<RAWINPUTDEVICE> &list) {
        UINT count = 0;
        if (GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) == UINT(-1))
            return false;
        list.resize(count);
        if (!count) return true;
        const UINT result = GetRegisteredRawInputDevices(list.data(), &count, sizeof(RAWINPUTDEVICE));
        if (result == UINT(-1)) return false;
        list.resize(result);
        return true;
    }
    bool Start() {
        if (enabled) return true;
        std::vector<RAWINPUTDEVICE> registered;
        if (!Registrations(registered)) { Error(L"Raw Input登録を取得できません"); return false; }
        bool hasPage = false;
        const HWND window = m_pApp->GetAppWindow();
        for (const auto &device : registered) {
            if (device.usUsagePage != ConsumerPage) continue;
            if (device.hwndTarget != window || (device.dwFlags & RIDEV_EXCLUDE)) {
                m_pApp->AddLog(L"HIDRemocon: 別ウィンドウのConsumer Raw Input登録と競合しています。",
                    TVTest::LOG_TYPE_ERROR);
                return false;
            }
            if (device.usUsage == 0 && (device.dwFlags & RIDEV_PAGEONLY)) hasPage = true;
        }
        // TVTest already registers 0C/01. Keep it intact. Page-only adds other
        // Consumer TLCs and targets the same host window used by the callback.
        if (!hasPage) {
            RAWINPUTDEVICE device{ConsumerPage, 0, RIDEV_PAGEONLY | RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window};
            if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
                Error(L"Consumer Raw Inputを登録できません"); return false;
            }
            ownsPageRegistration = true;
        }
        if (!m_pApp->SetWindowMessageCallback(MessageCallback, this)) {
            Stop();
            m_pApp->AddLog(L"HIDRemocon: メッセージコールバックを登録できません。", TVTest::LOG_TYPE_ERROR);
            return false;
        }
        enabled = true;
        return true;
    }
    void Stop() {
        if (enabled) m_pApp->SetWindowMessageCallback(nullptr);
        enabled = false;
        devices.clear();
        warnedUsages.clear();
        if (ownsPageRegistration) {
            std::vector<RAWINPUTDEVICE> registered;
            if (Registrations(registered)) {
                for (const auto &device : registered) {
                    if (device.usUsagePage == ConsumerPage && device.usUsage == 0
                            && device.hwndTarget == m_pApp->GetAppWindow()) {
                        RAWINPUTDEVICE remove{ConsumerPage, 0, RIDEV_REMOVE | RIDEV_PAGEONLY, nullptr};
                        if (!RegisterRawInputDevices(&remove, 1, sizeof(remove))) Error(L"Raw Input登録を解除できません");
                        break;
                    }
                }
            }
            ownsPageRegistration = false;
        }
    }
    bool Input(HRAWINPUT input) {
        UINT size = 0;
        if (GetRawInputData(input, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)
                || size < offsetof(RAWINPUT, data.hid.bRawData)) return false;
        std::vector<DWORD> buffer((size + sizeof(DWORD) - 1) / sizeof(DWORD));
        if (GetRawInputData(input, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) != size)
            return false;
        auto *raw = reinterpret_cast<RAWINPUT *>(buffer.data());
        if (raw->header.dwType != RIM_TYPEHID) return false;
        auto &hid = raw->data.hid;
        const size_t available = size - offsetof(RAWINPUT, data.hid.bRawData);
        if (!hid.dwSizeHid || hid.dwCount > available / hid.dwSizeHid) return false;
        auto found = devices.find(raw->header.hDevice);
        if (found == devices.end()) {
            auto device = std::make_unique<Device>();
            if (!device->Load(raw->header.hDevice)) return false;
            found = devices.emplace(raw->header.hDevice, std::move(device)).first;
        }
        bool handled = false;
        for (DWORD i = 0; i < hid.dwCount; ++i) {
            auto *report = reinterpret_cast<char *>(hid.bRawData + size_t(i) * hid.dwSizeHid);
            std::set<USHORT> current;
            if (!found->second->Decode(report, hid.dwSizeHid, current)) continue;
            handled = true;
            for (auto usage : found->second->state.Update(static_cast<UCHAR>(report[0]), current)) {
                if (usage <= LastUsage) {
                    m_pApp->OnControllerButtonDown(ControllerName, usage - 1);
                } else if (warnedUsages.insert(usage).second) {
                    wchar_t message[160];
                    swprintf_s(message, L"HIDRemocon: Usage 0x%04X は登録範囲外です。LastUsageを拡張してください。", usage);
                    m_pApp->AddLog(message, TVTest::LOG_TYPE_WARNING);
                }
            }
        }
        return handled;
    }
    static LRESULT CALLBACK EventCallback(UINT event, LPARAM param1, LPARAM, void *context) {
        auto *self = static_cast<HIDRemocon *>(context);
        if (event == TVTest::EVENT_PLUGINENABLE) {
            if (param1) return self->Start();
            self->Stop();
            return TRUE;
        }
        return 0;
    }
    static BOOL CALLBACK MessageCallback(HWND window, UINT message, WPARAM param1,
                                         LPARAM param2, LRESULT *result, void *context) {
        auto *self = static_cast<HIDRemocon *>(context);
        if (!self->enabled) return FALSE;
        if (message == WM_INPUT && self->Input(reinterpret_cast<HRAWINPUT>(param2))) {
            // The host's fixed-byte MCE decoder must not execute the same input.
            // DefWindowProc performs foreground WM_INPUT cleanup.
            *result = DefWindowProcW(window, message, param1, param2);
            return TRUE;
        }
        if (message == WM_INPUT_DEVICE_CHANGE) self->devices.erase(reinterpret_cast<HANDLE>(param2));
        if (message == WM_DEVICECHANGE && param1 == DBT_DEVNODES_CHANGED) self->devices.clear();
        return FALSE;
    }
public:
    bool GetPluginInfo(TVTest::PluginInfo *info) override {
        info->Type = TVTest::PLUGIN_TYPE_NORMAL;
        info->Flags = 0;
        info->pszPluginName = L"HIDコンシューマーリモコン";
        info->pszCopyright = L"Public Domain";
        info->pszDescription = L"機種を限定せずConsumer PageのHIDボタンを受信します。";
        return true;
    }
    bool Initialize() override {
        names.resize(LastUsage);
        buttons.resize(LastUsage);
        for (unsigned usage = 1; usage <= LastUsage; ++usage) {
            wchar_t fallback[24];
            swprintf_s(fallback, L"Usage 0x%04X", usage);
            names[usage - 1] = fallback;
        }
        for (const auto &usage : ConsumerUsageNames)
            if (usage.id && usage.id <= LastUsage) names[usage.id - 1] = usage.name;
        for (unsigned i = 0; i < LastUsage; ++i) buttons[i].pszName = names[i].c_str();
        TVTest::ControllerInfo info{};
        info.pszName = ControllerName;
        info.pszText = L"HID Consumer Control";
        info.NumButtons = static_cast<int>(buttons.size());
        info.pButtonList = buttons.data();
        info.pszSectionName = L"HIDConsumerControl";
        if (!m_pApp->RegisterController(&info)) {
            m_pApp->AddLog(L"HIDRemocon: コントローラを登録できません。", TVTest::LOG_TYPE_ERROR);
            return false;
        }
        return m_pApp->SetEventCallback(EventCallback, this);
    }
    bool Finalize() override { Stop(); return true; }
};

TVTest::CTVTestPlugin *CreatePluginClass() { return new HIDRemocon; }
