#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <vector>

// A report replaces only its own held buttons. Another report ID must not
// release those buttons. Each device owns a separate instance.
class ButtonState {
    std::map<std::uint8_t, std::set<std::uint16_t>> reports;
public:
    std::vector<std::uint16_t> Update(std::uint8_t reportId,
                                     const std::set<std::uint16_t> &current) {
        std::set<std::uint16_t> previous;
        for (const auto &report : reports)
            previous.insert(report.second.begin(), report.second.end());
        reports[reportId] = current;
        std::vector<std::uint16_t> pressed;
        for (auto usage : current)
            if (usage != 0 && previous.count(usage) == 0)
                pressed.push_back(usage);
        return pressed;
    }
};
