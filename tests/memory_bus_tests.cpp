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
    passed &= check(bus.read8(0xA0000102, byte) && byte == 0x56,
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
    std::uint8_t romByte = 0;
    passed &= check(bus.read8(N64MemoryBus::CartridgeBase + 2, romByte) &&
                    romByte == 0x12, "byte read works from cartridge ROM");
    passed &= check(!bus.write8(N64MemoryBus::CartridgeBase, 0),
                    "cartridge ROM rejects byte writes");
    passed &= check(!bus.write32(N64MemoryBus::CartridgeBase, 0),
                    "cartridge ROM is read-only");
    passed &= check(!bus.read32(N64MemoryBus::CartridgeBase + 8, word),
                    "out-of-range cartridge read is rejected");

    const std::vector<std::uint8_t> boot = {0x12, 0x34, 0x56, 0x78};
    bus.loadBootRom(boot);
    passed &= check(bus.read32(N64MemoryBus::BootRomBase, word) &&
                    word == 0x12345678, "boot ROM is mapped at reset physical address");
    passed &= check(bus.read8(N64MemoryBus::BootRomBase + 3, romByte) &&
                    romByte == 0x78, "byte read works from boot ROM");
    passed &= check(!bus.write8(N64MemoryBus::BootRomBase, 0),
                    "boot ROM rejects byte writes");
    passed &= check(!bus.write32(N64MemoryBus::BootRomBase, 0),
                    "boot ROM is read-only");

    // PIF RAM occupies the final 64 bytes of the PIF address window.
    passed &= check(bus.write32(N64MemoryBus::PifRamBase, 0xA1B2C3D4),
                    "PIF RAM accepts word writes");
    passed &= check(bus.read32(N64MemoryBus::PifRamBase, word) &&
                    word == 0xA1B2C3D4, "PIF RAM preserves big-endian word order");
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 2, byte) && byte == 0xC3,
                    "PIF RAM supports byte reads");
    passed &= check(bus.write8(N64MemoryBus::PifRamBase + 2, 0x55) &&
                    bus.read32(N64MemoryBus::PifRamBase, word) &&
                    word == 0xA1B255D4, "PIF byte writes preserve adjacent bytes");
    passed &= check(!bus.write32(N64MemoryBus::PifRamBase + 64, 0),
                    "writes beyond PIF RAM are rejected");

    // Execute a controller-identification Joybus packet through the PIF control byte.
    bus.pif().setControllerConnected(0, true);
    passed &= check(bus.write8(N64MemoryBus::PifRamBase + 0, 1), "write Joybus send length");
    passed &= check(bus.write8(N64MemoryBus::PifRamBase + 1, 3), "write Joybus receive length");
    passed &= check(bus.write8(N64MemoryBus::PifRamBase + 2, 0x00), "write controller identify command");
    passed &= check(bus.write8(N64MemoryBus::PifRamBase + 63, 0x01),
                    "PIF control byte starts Joybus processing");
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 3, byte) && byte == 0x05,
                    "controller identify returns gamepad type");
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 4, byte) && byte == 0x00,
                    "controller identify reports no accessory");
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 5, byte) && byte == 0x02,
                    "controller identify reports no Controller Pak");
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 63, byte) && byte == 0,
                    "PIF clears the Joybus execute bit after processing");

    bus.clearPifRam();
    bus.pif().setControllerState(0, 0x8001, -12, 34);
    bus.write8(N64MemoryBus::PifRamBase + 0, 1);
    bus.write8(N64MemoryBus::PifRamBase + 1, 4);
    bus.write8(N64MemoryBus::PifRamBase + 2, 0x01);
    bus.write8(N64MemoryBus::PifRamBase + 63, 0x01);
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 3, byte) && byte == 0x80,
                    "controller read returns button high byte");
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 4, byte) && byte == 0x01,
                    "controller read returns button low byte");
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 5, byte) && byte == 0xF4,
                    "controller read returns signed X axis");
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 6, byte) && byte == 0x22,
                    "controller read returns signed Y axis");

    bus.clearPifRam();
    bus.write8(N64MemoryBus::PifRamBase + 0, 1);
    bus.write8(N64MemoryBus::PifRamBase + 1, 3);
    bus.write8(N64MemoryBus::PifRamBase + 2, 0xFF);
    bus.write8(N64MemoryBus::PifRamBase + 63, 0x01);
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 3, byte) && byte == 0x05,
                    "controller reset returns the standard gamepad identifier");
    bus.clearPifRam();
    bus.write8(N64MemoryBus::PifRamBase + 0, 1);
    bus.write8(N64MemoryBus::PifRamBase + 1, 4);
    bus.write8(N64MemoryBus::PifRamBase + 2, 0x01);
    bus.write8(N64MemoryBus::PifRamBase + 63, 0x01);
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 5, byte) && byte == 0,
                    "controller reset centers the X stick axis");
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 6, byte) && byte == 0,
                    "controller reset centers the Y stick axis");

    bus.clearPifRam();
    bus.pif().setControllerConnected(0, false);
    bus.write8(N64MemoryBus::PifRamBase + 0, 1);
    bus.write8(N64MemoryBus::PifRamBase + 1, 3);
    bus.write8(N64MemoryBus::PifRamBase + 2, 0x00);
    bus.write8(N64MemoryBus::PifRamBase + 63, 0x01);
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 1, byte) && (byte & 0x80),
                    "disconnected controller sets the no-device response flag");

    // SI DMA transfers the 64-byte command block between RDRAM and PIF RAM.
    bus.clearPifRam();
    bus.pif().setControllerConnected(0, true);
    std::uint8_t joybusBlock[64] = {};
    joybusBlock[0] = 1;
    joybusBlock[1] = 3;
    joybusBlock[2] = 0x00;
    joybusBlock[63] = 0x01;
    for (std::size_t i = 0; i < 64; i += 4)
    {
        const std::uint32_t packed =
            (static_cast<std::uint32_t>(joybusBlock[i]) << 24) |
            (static_cast<std::uint32_t>(joybusBlock[i + 1]) << 16) |
            (static_cast<std::uint32_t>(joybusBlock[i + 2]) << 8) |
            static_cast<std::uint32_t>(joybusBlock[i + 3]);
        bus.write32(0x200 + i, packed);
    }
    bus.write32(N64MemoryBus::SiDramAddress, 0x200);
    passed &= check(bus.write32(N64MemoryBus::SiPifAddressWrite,
                                static_cast<std::uint32_t>(N64MemoryBus::PifRamBase)),
                    "SI DMA copies command block from RDRAM to PIF");
    passed &= check(bus.read8(N64MemoryBus::PifRamBase + 3, byte) && byte == 0x05,
                    "SI write DMA runs the PIF controller command");
    passed &= check(bus.write32(N64MemoryBus::SiPifAddressRead,
                                static_cast<std::uint32_t>(N64MemoryBus::PifRamBase)),
                    "SI DMA copies PIF response back to RDRAM");
    passed &= check(bus.read8(0x203, byte) && byte == 0x05,
                    "SI read DMA returns controller response to RDRAM");
    passed &= check(bus.read32(N64MemoryBus::SiStatus, word) && word == 0,
                    "SI status is idle after synchronous DMA completion");

    bus.write32(N64MemoryBus::SiDramAddress, 0x201);
    passed &= check(!bus.write32(N64MemoryBus::SiPifAddressWrite,
                                 static_cast<std::uint32_t>(N64MemoryBus::PifRamBase)),
                    "SI DMA rejects unaligned RDRAM addresses");
    passed &= check(bus.read32(N64MemoryBus::SiStatus, word) && (word & 0x08),
                    "SI reports an invalid DMA address");

    bus.clearRdram();
    passed &= check(bus.read32(0x100, word) && word == 0,
                    "clearing RDRAM resets its contents");

    if (!passed) return 1;
    std::cout << "All N64 memory bus tests passed.\n";
    return 0;
}
