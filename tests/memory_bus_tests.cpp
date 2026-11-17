#include "core/memory/n64_memory_bus.h"

#include <cstdint>
#include <iostream>
#include <vector>

static bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

static bool runJoybusCommand(N64MemoryBus &bus, std::uint8_t command,
                               std::uint8_t receiveLength, std::uint32_t dramAddress)
{
    std::uint8_t packet[64] = {};
    packet[0] = 1;
    packet[1] = receiveLength;
    packet[2] = command;
    packet[63] = 0x01;

    for (std::size_t i = 0; i < 64; i += 4)
    {
        const std::uint32_t word =
            (static_cast<std::uint32_t>(packet[i]) << 24) |
            (static_cast<std::uint32_t>(packet[i + 1]) << 16) |
            (static_cast<std::uint32_t>(packet[i + 2]) << 8) |
            static_cast<std::uint32_t>(packet[i + 3]);
        if (!bus.write32(dramAddress + static_cast<std::uint32_t>(i), word))
            return false;
    }

    return bus.write32(N64MemoryBus::SiDramAddress, dramAddress) &&
           bus.write32(N64MemoryBus::SiPifAddressWrite,
                       static_cast<std::uint32_t>(N64MemoryBus::PifRamBase)) &&
           bus.write32(N64MemoryBus::SiPifAddressRead,
                       static_cast<std::uint32_t>(N64MemoryBus::PifRamBase));
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

    passed &= check(bus.write32(N64MemoryBus::SpDmemBase, 0x11223344),
                    "write to RSP data memory");
    passed &= check(bus.read32(N64MemoryBus::SpDmemBase, word) && word == 0x11223344,
                    "read back RSP data memory");
    passed &= check(bus.read32(0xA4000000, word) && word == 0x11223344,
                    "KSEG1 maps to RSP data memory");
    passed &= check(bus.write32(N64MemoryBus::SpImemBase, 0xAABBCCDD),
                    "write to RSP instruction memory");
    passed &= check(bus.read32(0xA4001000, word) && word == 0xAABBCCDD,
                    "KSEG1 maps to RSP instruction memory");
    passed &= check(!bus.write32(N64MemoryBus::SpImemBase + N64MemoryBus::SpMemorySize, 0),
                    "RSP data memory rejects out-of-range writes");

    // SP DMA moves data between RDRAM and the selected DMEM/IMEM bank.
    bus.write32(0xA00, 0x11223344);
    bus.write32(0xA04, 0x55667788);
    passed &= check(bus.write32(N64MemoryBus::SpMemoryAddress, 0x0000),
                    "select SP DMEM for DMA");
    passed &= check(bus.write32(N64MemoryBus::SpDramAddress, 0xA00),
                    "set SP DMA source in RDRAM");
    passed &= check(bus.write32(N64MemoryBus::SpReadLength, 7),
                    "SP read-length starts an 8-byte RDRAM-to-DMEM DMA");
    passed &= check(bus.read32(N64MemoryBus::SpDmemBase, word) && word == 0x11223344,
                    "SP DMA copies first word into DMEM");
    passed &= check(bus.read32(N64MemoryBus::SpDmemBase + 4, word) && word == 0x55667788,
                    "SP DMA copies second word into DMEM");

    passed &= check(bus.write32(N64MemoryBus::SpMemoryAddress, 0x1000),
                    "select SP IMEM for DMA");
    passed &= check(bus.write32(N64MemoryBus::SpDramAddress, 0xA00),
                    "set SP IMEM DMA source");
    passed &= check(bus.write32(N64MemoryBus::SpReadLength, 7),
                    "SP read-length starts RDRAM-to-IMEM DMA");
    passed &= check(bus.read32(N64MemoryBus::SpImemBase, word) && word == 0x11223344,
                    "SP DMA copies data into IMEM");

    passed &= check(bus.write32(N64MemoryBus::SpMemoryAddress, 0),
                    "select DMEM for reverse DMA");
    passed &= check(bus.write32(N64MemoryBus::SpDramAddress, 0xB00),
                    "set SP DMA destination in RDRAM");
    passed &= check(bus.write32(N64MemoryBus::SpWriteLength, 7),
                    "SP write-length starts an 8-byte DMEM-to-RDRAM DMA");
    passed &= check(bus.read32(0xB00, word) && word == 0x11223344,
                    "SP DMA copies DMEM back into RDRAM");
    passed &= check(bus.read32(0xB04, word) && word == 0x55667788,
                    "SP DMA copies the full DMEM block back into RDRAM");

    bus.write32(0xC00, 0xAAAABBBB);
    bus.write32(0xC08, 0xEEEEFFFF);
    bus.write32(N64MemoryBus::SpMemoryAddress, 0x0020);
    bus.write32(N64MemoryBus::SpDramAddress, 0xC00);
    passed &= check(bus.write32(N64MemoryBus::SpReadLength, 0x00401003),
                    "SP DMA supports two blocks with a four-byte DRAM skip");
    passed &= check(bus.read32(N64MemoryBus::SpDmemBase + 0x20, word) && word == 0xAAAABBBB,
                    "first SP DMA block reaches DMEM");
    passed &= check(bus.read32(N64MemoryBus::SpDmemBase + 0x24, word) && word == 0xEEEEFFFF,
                    "second SP DMA block honours DRAM skip");

    bus.write32(N64MemoryBus::SpDramAddress, N64MemoryBus::RdramSize - 4);
    passed &= check(!bus.write32(N64MemoryBus::SpReadLength, 7),
                    "SP DMA rejects transfers beyond RDRAM bounds");
    passed &= check(bus.read32(N64MemoryBus::SpDmaBusy, word) && word == 0,
                    "SP DMA busy register is clear after synchronous transfers");
    passed &= check(bus.read32(N64MemoryBus::SpSemaphore, word) && word == 0,
                    "SP semaphore read returns its previous value");
    passed &= check(bus.read32(N64MemoryBus::SpSemaphore, word) && word == 1,
                    "SP semaphore becomes locked after read");
    passed &= check(bus.write32(N64MemoryBus::SpSemaphore, 0) &&
                    bus.read32(N64MemoryBus::SpSemaphore, word) && word == 0,
                    "writing SP semaphore releases the lock");
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

    // VI register block and early framebuffer conversion.
    std::vector<std::uint32_t> frame;
    passed &= check(bus.write32(N64MemoryBus::ViControl, 2), "VI selects 16-bit RGBA mode");
    passed &= check(bus.write32(N64MemoryBus::ViOrigin, 0x1000), "VI framebuffer origin");
    passed &= check(bus.write32(N64MemoryBus::ViWidth, 2), "VI framebuffer width");
    passed &= check(bus.write8(0x1000, 0xF8) && bus.write8(0x1001, 0x01) &&
                    bus.write8(0x1002, 0x07) && bus.write8(0x1003, 0xC1),
                    "write RGBA5551 framebuffer pixels");
    passed &= check(bus.read32(N64MemoryBus::ViControl, word) && word == 2,
                    "VI control register reads back");
    passed &= check(bus.renderFramebuffer(2, 1, frame) && frame.size() == 2 &&
                    frame[0] == 0xFFFF0000U && frame[1] == 0xFF00FF00U,
                    "VI converts RGBA5551 to ARGB pixels");
    passed &= check(bus.write32(N64MemoryBus::ViControl, 3), "VI selects 32-bit RGBA mode");
    passed &= check(bus.write8(0x1000, 0x12) && bus.write8(0x1001, 0x34) &&
                    bus.write8(0x1002, 0x56) && bus.write8(0x1003, 0x78),
                    "write RGBA8888 framebuffer pixel");
    passed &= check(bus.renderFramebuffer(1, 1, frame) && frame[0] == 0x78123456U,
                    "VI converts RGBA8888 to ARGB pixels");
    passed &= check(!bus.renderFramebuffer(3, 1, frame), "VI rejects width beyond VI_WIDTH");
    passed &= check(bus.write32(N64MemoryBus::ViOrigin,
                                static_cast<std::uint32_t>(N64MemoryBus::RdramSize - 2)) &&
                    !bus.renderFramebuffer(2, 1, frame), "VI rejects out-of-bounds framebuffer");

    passed &= check(bus.write32(N64MemoryBus::ViControl, 0),
                    "unsupported VI pixel format is rejected");
    passed &= check(!bus.renderFramebuffer(1, 1, frame),
                    "VI does not render when pixel format is disabled");
    passed &= check(bus.write32(N64MemoryBus::ViControl, 2) &&
                    bus.write32(N64MemoryBus::ViOrigin, 0x1000) &&
                    bus.write32(N64MemoryBus::ViWidth, 2),
                    "restore VI registers for following tests");

    // VI scanline counter, frame wrap, and MI interrupt behaviour.
    passed &= check(bus.write32(N64MemoryBus::ViVSync, 4),
                    "configure a four-line VI frame");
    passed &= check(bus.write32(N64MemoryBus::ViInterrupt, 2),
                    "configure VI interrupt scanline");
    passed &= check(bus.write32(N64MemoryBus::MiInterruptMask, 0x80),
                    "enable VI interrupt in MI mask");
    bus.advanceViScanlines(1);
    passed &= check(bus.read32(N64MemoryBus::ViCurrent, word) && word == 1,
                    "VI current register advances one scanline");
    passed &= check((bus.read32(N64MemoryBus::MiInterrupt, word) && (word & 0x08U) == 0),
                    "VI interrupt stays clear before compare scanline");
    bus.advanceViScanlines(1);
    passed &= check(bus.read32(N64MemoryBus::ViCurrent, word) && word == 2,
                    "VI current reaches programmed compare scanline");
    passed &= check(bus.read32(N64MemoryBus::MiInterrupt, word) && (word & 0x08U) != 0,
                    "VI compare raises MI VI interrupt");
    passed &= check(bus.interruptPending(), "enabled VI interrupt reaches CPU interrupt line");
    passed &= check(bus.write32(N64MemoryBus::ViCurrent, 0),
                    "writing VI current acknowledges the interrupt");
    passed &= check(bus.read32(N64MemoryBus::MiInterrupt, word) && (word & 0x08U) == 0,
                    "VI acknowledgement clears MI VI interrupt");
    bus.advanceViScanlines(2);
    passed &= check(bus.read32(N64MemoryBus::ViCurrent, word) && word == 0,
                    "VI current wraps at VI_V_SYNC");
    bus.advanceViScanlines(2);
    passed &= check(bus.read32(N64MemoryBus::MiInterrupt, word) && (word & 0x08U) != 0,
                    "VI compare interrupt fires again on the next frame");
    passed &= check(bus.write32(N64MemoryBus::ViCurrent, 0),
                    "acknowledge second VI frame interrupt");
    passed &= check(bus.write32(N64MemoryBus::MiInterruptMask, 0x40),
                    "disable VI interrupt in MI mask");

    passed &= check(bus.write32(N64MemoryBus::ViVSync, 4) &&
                    bus.write32(N64MemoryBus::ViCurrent, 0),
                    "acknowledge VI interrupt before cycle-clock test");
    // VI_CURRENT is read-only; writing it acknowledges the interrupt but
    // does not reset the scanline counter. Advance from line 2 to frame line 0.
    bus.advanceViScanlines(2);
    passed &= check(bus.read32(N64MemoryBus::ViCurrent, word) && word == 0,
                    "cycle-clock test starts at the beginning of a VI frame");
    bus.advanceViCycles(29761);
    passed &= check(bus.read32(N64MemoryBus::ViCurrent, word) && word == 0,
                    "VI does not advance before a full scanline of CPU cycles");
    bus.advanceViCycles(1);
    passed &= check(bus.read32(N64MemoryBus::ViCurrent, word) && word == 1,
                    "VI advances after one NTSC scanline worth of CPU cycles");
    bus.advanceViCycles(29762U * 3U);
    passed &= check(bus.read32(N64MemoryBus::ViCurrent, word) && word == 0,
                    "cycle-driven VI timing wraps at the configured frame length");
    passed &= check(bus.write32(N64MemoryBus::ViCurrent, 0),
                    "acknowledge cycle-driven VI interrupt before PI tests");

    // PI DMA copies cartridge ROM into RDRAM and can copy RDRAM into save RAM.
    passed &= check(bus.write32(N64MemoryBus::PiDramAddress, 0x800),
                    "set PI destination RDRAM address");
    passed &= check(bus.write32(N64MemoryBus::PiCartAddress,
                                static_cast<std::uint32_t>(N64MemoryBus::CartridgeBase)),
                    "set PI source cartridge address");
    passed &= check(bus.write32(N64MemoryBus::PiWriteLength, 7),
                    "PI write-length register starts an 8-byte cartridge DMA");
    passed &= check(bus.read32(0x800, word) && word == 0x80371240,
                    "PI DMA copies first cartridge word into RDRAM");
    passed &= check(bus.read32(0x804, word) && word == 0xAABBCCDD,
                    "PI DMA copies second cartridge word into RDRAM");
    passed &= check(bus.read32(N64MemoryBus::PiStatus, word) && word == 0x08,
                    "PI status reports DMA completion interrupt");
    passed &= check(bus.read32(N64MemoryBus::MiInterrupt, word) && word == 0x10,
                    "PI DMA raises the MI PI interrupt bit");
    passed &= check(bus.write32(N64MemoryBus::MiInterruptMask, 0x200),
                    "enable PI interrupt in MI mask");
    passed &= check(bus.interruptPending(),
                    "enabled PI interrupt reaches the CPU interrupt line");
    passed &= check(bus.write32(N64MemoryBus::PiStatus, 0x02),
                    "PI status acknowledges completion interrupt");
    passed &= check(bus.read32(N64MemoryBus::MiInterrupt, word) && word == 0,
                    "PI acknowledgement clears the MI PI interrupt");
    passed &= check(bus.write32(N64MemoryBus::MiInterruptMask, 0x100),
                    "disable PI interrupt in MI mask");

    passed &= check(bus.write32(0x900, 0xDEADBEEF), "prepare RDRAM data for save RAM");
    passed &= check(bus.write32(0x904, 0x12345678), "prepare second save RAM word");
    passed &= check(bus.write32(N64MemoryBus::PiDramAddress, 0x900), "set PI save DMA source");
    passed &= check(bus.write32(N64MemoryBus::PiCartAddress,
                                static_cast<std::uint32_t>(N64MemoryBus::SaveRamBase)),
                    "set PI save RAM destination");
    passed &= check(bus.write32(N64MemoryBus::PiReadLength, 7),
                    "PI read-length register starts RDRAM-to-save-RAM DMA");
    passed &= check(bus.read32(N64MemoryBus::SaveRamBase, word) && word == 0xDEADBEEF,
                    "PI DMA stores first word in cartridge save RAM");
    passed &= check(bus.read32(N64MemoryBus::SaveRamBase + 4, word) && word == 0x12345678,
                    "PI DMA stores second word in cartridge save RAM");
    passed &= check(bus.write32(N64MemoryBus::PiStatus, 0x02),
                    "acknowledge save RAM PI DMA interrupt");

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

    // Joybus commands are executed by SI DMA, not by direct CPU writes to PIF RAM.
    bus.pif().setControllerConnected(0, true);
    passed &= check(runJoybusCommand(bus, 0x00, 3, 0x300),
                    "SI DMA executes controller-identification command");
    passed &= check(bus.read8(0x303, byte) && byte == 0x05,
                    "controller identify returns gamepad type");
    passed &= check(bus.read8(0x304, byte) && byte == 0x00,
                    "controller identify returns the standard device ID");
    passed &= check(bus.read8(0x305, byte) && byte == 0x02,
                    "controller identify reports no Controller Pak");
    passed &= check(bus.read8(0x33F, byte) && byte == 0,
                    "PIF clears the Joybus execute bit after SI processing");

    bus.pif().setControllerState(0, 0x8001, -12, 34);
    passed &= check(runJoybusCommand(bus, 0x01, 4, 0x400),
                    "SI DMA executes controller input-read command");
    passed &= check(bus.read8(0x403, byte) && byte == 0x80,
                    "controller read returns button high byte");
    passed &= check(bus.read8(0x404, byte) && byte == 0x01,
                    "controller read returns button low byte");
    passed &= check(bus.read8(0x405, byte) && byte == 0xF4,
                    "controller read returns signed X axis");
    passed &= check(bus.read8(0x406, byte) && byte == 0x22,
                    "controller read returns signed Y axis");

    passed &= check(runJoybusCommand(bus, 0xFF, 3, 0x500),
                    "SI DMA executes controller reset command");
    passed &= check(bus.read8(0x503, byte) && byte == 0x05,
                    "controller reset returns the standard gamepad identifier");
    passed &= check(runJoybusCommand(bus, 0x01, 4, 0x600),
                    "controller can be polled after reset");
    passed &= check(bus.read8(0x605, byte) && byte == 0,
                    "controller reset centers the X stick axis");
    passed &= check(bus.read8(0x606, byte) && byte == 0,
                    "controller reset centers the Y stick axis");

    bus.pif().setControllerConnected(0, false);
    passed &= check(runJoybusCommand(bus, 0x00, 3, 0x700),
                    "SI DMA handles an absent controller");
    passed &= check(bus.read8(0x701, byte) && (byte & 0x80),
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
    passed &= check(bus.read32(N64MemoryBus::SiStatus, word) && word == 0x1000,
                    "SI status reports DMA completion interrupt");
    passed &= check(bus.read32(N64MemoryBus::MiInterrupt, word) && word == 0x02,
                    "SI DMA raises the MI SI interrupt bit");
    passed &= check(bus.write32(N64MemoryBus::MiInterruptMask, 0x08),
                    "MI mask register enables the SI interrupt");
    passed &= check(bus.read32(N64MemoryBus::MiInterruptMask, word) && word == 0x02,
                    "MI mask register reports the enabled SI interrupt");
    passed &= check(bus.interruptPending(),
                    "enabled SI interrupt reaches the CPU interrupt line");
    passed &= check(bus.write32(N64MemoryBus::SiStatus, 0),
                    "writing SI status clears the SI interrupt");
    passed &= check(bus.read32(N64MemoryBus::MiInterrupt, word) && word == 0,
                    "clearing SI status lowers the MI SI interrupt");
    passed &= check(!bus.interruptPending(),
                    "cleared SI interrupt no longer reaches the CPU");

    bus.write32(N64MemoryBus::SiDramAddress, 0x201);
    passed &= check(!bus.write32(N64MemoryBus::SiPifAddressWrite,
                                 static_cast<std::uint32_t>(N64MemoryBus::PifRamBase)),
                    "SI DMA rejects unaligned RDRAM addresses");
    passed &= check(bus.read32(N64MemoryBus::SiStatus, word) && (word & 0x08),
                    "SI reports an invalid DMA address");

    bus.clearRdram();
    passed &= check(bus.read32(0x100, word) && word == 0,
                    "clearing RDRAM resets its contents");
    passed &= check(bus.read32(N64MemoryBus::SpDmemBase, word) && word == 0,
                    "system RAM reset also clears RSP data memory");
    passed &= check(bus.read32(N64MemoryBus::SpImemBase, word) && word == 0,
                    "system RAM reset also clears RSP instruction memory");

    if (!passed) return 1;
    std::cout << "All N64 memory bus tests passed.\n";
    return 0;
}
