#include "core/n64_system.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\\n';
    return condition;
}

int main()
{
    bool passed = true;
    N64System system;
    std::string error;

    // addiu $t0, $zero, 42
    // addiu $t1, $t0, 1
    const std::vector<std::uint8_t> bootRom = {
        0x24, 0x08, 0x00, 0x2A,
        0x25, 0x09, 0x00, 0x01
    };
    system.loadBootRom(bootRom);

    passed &= check(system.cpu().pc() == R4300i::ResetAddress,
                    "system reset starts CPU at the N64 boot address");
    passed &= check(system.step(error), "CPU fetches and executes from mapped boot ROM");
    passed &= check(system.cpu().reg(8) == 42,
                    "first boot ROM instruction updates register t0");
    passed &= check(system.cpu().pc() == R4300i::ResetAddress + 4,
                    "CPU advances after executing boot ROM instruction");

    passed &= check(system.step(error), "CPU executes the next boot ROM instruction");
    passed &= check(system.cpu().reg(9) == 43,
                    "second instruction reads the result of the first");
    passed &= check(system.cpu().cp0(R4300i::Count) == 2,
                    "CP0 Count advances for executed instructions");

    system.reset();
    passed &= check(system.cpu().pc() == R4300i::ResetAddress,
                    "system reset restores CPU boot address");
    passed &= check(system.cpu().reg(8) == 0,
                    "system reset clears CPU general registers");

    if (!passed) return 1;
    std::cout << "All N64 system integration tests passed.\\n";
    return 0;
}
