#include "core/n64_system.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
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

    // The system exposes a separately stepped scalar RSP sharing SP IMEM/DMEM.
    passed &= check(system.memory().write32(N64MemoryBus::SpImemBase, 0x2408002A),
                    "write RSP test instruction through system memory bus");
    passed &= check(system.memory().write32(N64MemoryBus::SpImemBase + 4, 0x0000000D),
                    "write RSP BREAK instruction");
    passed &= check(system.memory().write32(N64MemoryBus::SpStatus, 0x01),
                    "clear SP HALT to start the RSP");
    passed &= check(system.stepRsp(error), "system steps its RSP processor");
    passed &= check(system.rsp().reg(8) == 42 && system.rsp().pc() == 4,
                    "system RSP executes from shared instruction memory");
    passed &= check(system.stepRsp(error), "system steps RSP BREAK");
    std::uint32_t spStatus = 0;
    passed &= check(system.memory().read32(N64MemoryBus::SpStatus, spStatus) &&
                    (spStatus & 0x03U) == 0x03U,
                    "RSP BREAK sets SP HALT and BROKE status bits");

    passed &= check(system.memory().write32(N64MemoryBus::PifRamBase, 0xDEADBEEF),
                    "system test can write PIF RAM before reset");

    system.reset();
    passed &= check(system.cpu().pc() == R4300i::ResetAddress,
                    "system reset restores CPU boot address");
    passed &= check(system.cpu().reg(8) == 0,
                    "system reset clears CPU general registers");
    passed &= check(system.rsp().pc() == 0 && system.rsp().reg(8) == 0,
                    "system reset clears the RSP processor state");
    std::uint32_t pifWord = 1;
    passed &= check(system.memory().read32(N64MemoryBus::PifRamBase, pifWord) &&
                    pifWord == 0, "system reset clears PIF RAM");

    // SI DMA completion raises an MI interrupt, which reaches the R4300i IP2 line.
    passed &= check(system.memory().write32(N64MemoryBus::SiDramAddress, 0x100),
                    "set RDRAM address for SI interrupt test");
    passed &= check(system.memory().write32(N64MemoryBus::SiPifAddressWrite,
                                            static_cast<std::uint32_t>(N64MemoryBus::PifRamBase)),
                    "SI DMA completes and raises its interrupt");
    passed &= check(system.memory().write32(N64MemoryBus::MiInterruptMask, 0x08),
                    "enable SI interrupt in MI");
    system.cpu().setCp0(R4300i::Status, 0x00400401);
    passed &= check(system.memory().interruptPending(),
                    "MI forwards enabled SI interrupt to CPU");
    passed &= check(system.step(error), "CPU accepts enabled hardware interrupt");
    passed &= check(system.cpu().pc() == R4300i::BootstrapExceptionVector,
                    "hardware interrupt selects bootstrap exception vector");
    passed &= check(system.cpu().cp0(R4300i::EPC) == R4300i::ResetAddress,
                    "hardware interrupt records interrupted PC in EPC");
    passed &= check(((system.cpu().cp0(R4300i::Cause) >> 2) & 0x1F) == 0,
                    "hardware interrupt records exception code zero");
    passed &= check((system.cpu().cp0(R4300i::Cause) & (1ULL << 10)) != 0,
                    "hardware interrupt sets the external IP2 pending bit");
    passed &= check(system.memory().write32(N64MemoryBus::SiStatus, 0),
                    "SI status write acknowledges the interrupt");
    passed &= check(!system.memory().interruptPending(),
                    "acknowledged SI interrupt is no longer pending");

    if (!passed) return 1;
    std::cout << "All N64 system integration tests passed.\n";
    return 0;
}
