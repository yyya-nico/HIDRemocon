// Use installed HID descriptors and synthesized reports. This does not send
// input to Windows or change hardware state.
#include "../HIDRemocon.cpp"
#include <iostream>
#include <cstdlib>

static void Check(bool condition) {
    if (!condition) { std::cerr << "HID parser check FAILED\n"; std::exit(1); }
}
int main() {
    UINT count = 0;
    Check(GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != UINT(-1));
    std::vector<RAWINPUTDEVICELIST> list(count);
    if (count) Check(GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST)) != UINT(-1));
    int descriptors = 0, reports = 0;
    for (const auto &entry : list) {
        if (entry.dwType != RIM_TYPEHID) continue;
        Device device;
        if (!device.Load(entry.hDevice)) continue;
        bool used = false;
        std::set<UCHAR> testedIds;
        for (const auto &cap : device.buttons) {
            if (cap.UsagePage != ConsumerPage || !testedIds.insert(cap.ReportID).second) continue;
            std::vector<char> report(device.caps.InputReportByteLength);
            Check(HidP_InitializeReportForID(HidP_Input, cap.ReportID, device.Data(), report.data(),
                static_cast<ULONG>(report.size())) == HIDP_STATUS_SUCCESS);
            USAGE usage = cap.IsRange ? cap.Range.UsageMin : cap.NotRange.Usage;
            if (!usage && cap.IsRange && cap.Range.UsageMax > 0) ++usage;
            if (!usage) continue;
            ULONG usages = 1;
            if (HidP_SetUsages(HidP_Input, ConsumerPage, cap.LinkCollection, &usage, &usages,
                    device.Data(), report.data(), static_cast<ULONG>(report.size())) != HIDP_STATUS_SUCCESS)
                continue;
            std::set<USHORT> decoded;
            Check(device.Decode(report.data(), static_cast<ULONG>(report.size()), decoded));
            Check(decoded.count(usage) == 1);
            Check(!device.Decode(report.data(), static_cast<ULONG>(report.size() - 1), decoded));
            Check(HidP_UnsetUsages(HidP_Input, ConsumerPage, cap.LinkCollection, &usage, &usages,
                device.Data(), report.data(), static_cast<ULONG>(report.size())) == HIDP_STATUS_SUCCESS);
            decoded.clear();
            Check(device.Decode(report.data(), static_cast<ULONG>(report.size()), decoded));
            Check(decoded.count(usage) == 0);
            ++reports;
            used = true;
        }
        if (used) ++descriptors;
    }
    std::cout << "HID parser: " << reports << " synthesized press/release reports using "
        << descriptors << " installed HID descriptors passed\n";
    if (!reports) std::cout << "No suitable Consumer button descriptor found; hardware parser checks skipped\n";
}
