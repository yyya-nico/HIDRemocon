#include "../ButtonState.h"
#include <cstdlib>
#include <iostream>

static void Check(bool condition) {
    if (!condition) { std::cerr << "FAILED\n"; std::exit(1); }
}

int main() {
    ButtonState remote, keyboard;
    Check(remote.Update(2, {0x223}) == std::vector<std::uint16_t>{0x223});
    Check(remote.Update(2, {0x223}).empty()); // held report, no double execution
    Check(remote.Update(3, {}).empty()); // unrelated report does not release ID 2
    Check(remote.Update(2, {0x223}).empty());
    Check(remote.Update(2, {0x223, 0xE2}) == std::vector<std::uint16_t>{0xE2});
    Check(keyboard.Update(2, {0x223}) == std::vector<std::uint16_t>{0x223});
    Check(remote.Update(2, {}).empty());
    Check(remote.Update(2, {0x223}) == std::vector<std::uint16_t>{0x223});
    Check(remote.Update(3, {0x223}).empty()); // same usage held on a second report
    Check(remote.Update(2, {}).empty());
    Check(remote.Update(3, {0x223}).empty());
    Check(remote.Update(3, {}).empty());
    Check(remote.Update(3, {0, 0x223}) == std::vector<std::uint16_t>{0x223});
    Check(remote.Update(3, {0xE2}) == std::vector<std::uint16_t>{0xE2});
    std::cout << "14 button-state checks passed\n";
}
