#include "core/memory/n64_memory_bus.h"

#include <cstdint>
#include <iostream>
#include <vector>

static bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

int main()
{
    bool passed = true;
    N64MemoryBus bus;
    std::uint32_t word = 0;

    passed &= check(bus.write32(0x00000100, 0x12345678), "write to physical RDRAM");
    passed &= check(bus.read32(0x00000100, word) && word == 0x12345678,
                    "read back big-endian RDRAM word");
    passed &= check(bus.read32(0x80000100, word) && word == 0x12345678,
                    "KSEG0 address aliases physical RDRAM");
    passed &= check(bus.read32(0xA0000100, word) && word == 0x12345678,
                    "KSEG1 address aliases physical RDRAM");
    passed &= check(!bus.write32(0x80000001, 0xFFFFFFFF),
                    "unaligned word write is rejected");

    std::uint8_t byte = 0;
    passed &= check(bus.read8(0xA0000101, byte) && byte == 0x56,
                    "inherited byte read uses big-endian byte order");
    passed &= check(bus.write8(0x80000102, 0xAB), "byte write to RDRAM");
    passed &= check(bus.read32(0x100, word) && word == 0x1234AB78,
                    "byte write preserves neighboring bytes");

    const std::vector<std::uint8_t> cartridge = {
        0x80, 0x37, 0x12, 0x40, 0xAA, 0xBB, 0xCC, 0xDD
    };
    bus.loadCartridge(cartridge);
    passed &= check(bus.cartridgeSize() == 8, "cartridge image size is reported");
    passed &= check(bus.read32(N64MemoryBus::CartridgeBase, word) &&
                    word == 0x80371240, "cartridge is mapped at its physical base");
    passed &= check(!bus.write32(N64MemoryBus::CartridgeBase, 0),
                    "cartridge ROM is read-only");
    passed &= check(!bus.read32(N64MemoryBus::CartridgeBase + 8, word),
                    "out-of-range cartridge read is rejected");

    const std::vector<std::uint8_t> boot = {0x12, 0x34, 0x56, 0x78};
    bus.loadBootRom(boot);
    passed &= check(bus.read32(N64MemoryBus::BootRomBase, word) &&
                    word == 0x12345678, "boot ROM is mapped at reset physical address");
    passed &= check(!bus.write32(N64MemoryBus::BootRomBase, 0),
                    "boot ROM is read-only");

    bus.clearRdram();
    passed &= check(bus.read32(0x100, word) && word == 0,
                    "clearing RDRAM resets its contents");

    if (!passed) return 1;
    std::cout << "All N64 memory bus tests passed.\n";
    return 0;
}
