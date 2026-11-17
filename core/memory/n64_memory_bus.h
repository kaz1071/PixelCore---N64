#pragma once

#include "core/cpu/r4300i.h"
#include "core/pif/pif.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Early physical-memory implementation for the N64.
// RDRAM is 8 MiB; cartridge and boot ROM images are read-only.
class N64MemoryBus final : public N64Memory
{
public:
    static constexpr std::size_t RdramSize = 8 * 1024 * 1024;
    static constexpr std::uint64_t CartridgeBase = 0x10000000ULL;
    static constexpr std::uint64_t BootRomBase = 0x1FC00000ULL;
    static constexpr std::uint64_t PifRamBase = 0x1FC007C0ULL;
    static constexpr std::uint64_t SpDmemBase = 0x04000000ULL;
    static constexpr std::uint64_t SpImemBase = 0x04001000ULL;
    static constexpr std::size_t SpMemorySize = 4 * 1024;
    static constexpr std::uint64_t SpRegisterBase = 0x04040000ULL;
    static constexpr std::uint64_t SpMemoryAddress = SpRegisterBase;
    static constexpr std::uint64_t SpDramAddress = SpRegisterBase + 0x04;
    static constexpr std::uint64_t SpReadLength = SpRegisterBase + 0x08;
    static constexpr std::uint64_t SpWriteLength = SpRegisterBase + 0x0C;
    static constexpr std::uint64_t SpStatus = SpRegisterBase + 0x10;
    static constexpr std::uint64_t SpDmaBusy = SpRegisterBase + 0x18;
    static constexpr std::uint64_t SpSemaphore = SpRegisterBase + 0x1C;
    static constexpr std::uint64_t SpProgramCounter = 0x04080000ULL;
    static constexpr std::uint64_t PiRegisterBase = 0x04600000ULL;
    static constexpr std::uint64_t PiDramAddress = PiRegisterBase;
    static constexpr std::uint64_t PiCartAddress = PiRegisterBase + 0x04;
    static constexpr std::uint64_t PiReadLength = PiRegisterBase + 0x08;
    static constexpr std::uint64_t PiWriteLength = PiRegisterBase + 0x0C;
    static constexpr std::uint64_t PiStatus = PiRegisterBase + 0x10;
    static constexpr std::uint64_t SaveRamBase = 0x08000000ULL;
    static constexpr std::size_t SaveRamSize = 128 * 1024;
    static constexpr std::uint64_t SiRegisterBase = 0x04800000ULL;
    static constexpr std::uint64_t SiDramAddress = SiRegisterBase;
    static constexpr std::uint64_t SiPifAddressRead = SiRegisterBase + 0x04;
    static constexpr std::uint64_t SiPifAddressWrite = SiRegisterBase + 0x10;
    static constexpr std::uint64_t SiStatus = SiRegisterBase + 0x18;
    static constexpr std::uint64_t MiRegisterBase = 0x04300000ULL;
    static constexpr std::uint64_t MiVersion = MiRegisterBase + 0x04;
    static constexpr std::uint64_t MiInterrupt = MiRegisterBase + 0x08;
    static constexpr std::uint64_t MiInterruptMask = MiRegisterBase + 0x0C;
    static constexpr std::uint64_t ViRegisterBase = 0x04400000ULL;
    static constexpr std::uint64_t ViControl = ViRegisterBase;
    static constexpr std::uint64_t ViOrigin = ViRegisterBase + 0x04;
    static constexpr std::uint64_t ViWidth = ViRegisterBase + 0x08;
    static constexpr std::uint64_t ViInterrupt = ViRegisterBase + 0x0C;
    static constexpr std::uint64_t ViCurrent = ViRegisterBase + 0x10;
    static constexpr std::uint64_t ViBurst = ViRegisterBase + 0x14;
    static constexpr std::uint64_t ViVSync = ViRegisterBase + 0x18;
    static constexpr std::uint64_t ViHSync = ViRegisterBase + 0x1C;
    static constexpr std::uint64_t ViLeap = ViRegisterBase + 0x20;
    static constexpr std::uint64_t ViHStart = ViRegisterBase + 0x24;
    static constexpr std::uint64_t ViVStart = ViRegisterBase + 0x28;
    static constexpr std::uint64_t ViVBurst = ViRegisterBase + 0x2C;
    static constexpr std::uint64_t ViXScale = ViRegisterBase + 0x30;
    static constexpr std::uint64_t ViYScale = ViRegisterBase + 0x34;
    static constexpr std::size_t ViRegisterCount = 14;
    static constexpr std::size_t PifRamSize = 64;

    N64MemoryBus();

    bool read8(std::uint64_t address, std::uint8_t &value) override;
    bool write8(std::uint64_t address, std::uint8_t value) override;
    bool read32(std::uint64_t address, std::uint32_t &value) override;
    bool write32(std::uint64_t address, std::uint32_t value) override;
    bool interruptPending() const override;
    bool spHalted() const;
    std::uint32_t spProgramCounter() const;
    void setSpProgramCounter(std::uint32_t address);
    void setSpHalted(bool halted);
    void setSpBroke(bool broke);

    void loadCartridge(const std::vector<std::uint8_t> &image);
    void loadBootRom(const std::vector<std::uint8_t> &image);
    void clearRdram();
    void clearPifRam();
    Pif &pif();
    const Pif &pif() const;

    std::size_t cartridgeSize() const;
    std::size_t bootRomSize() const;
    bool renderFramebuffer(std::uint32_t width, std::uint32_t height, std::vector<std::uint32_t> &pixels) const;
    void advanceViScanlines(std::uint32_t scanlines);
    void advanceViCycles(std::uint32_t cpuCycles);

private:
    static std::uint64_t toPhysicalAddress(std::uint64_t address);
    void processPifControl();
    bool transferRdramToPif();
    bool transferPifToRdram();
    bool transferCartridgeToRdram(std::uint32_t lengthMinusOne);
    bool transferRdramToSaveRam(std::uint32_t lengthMinusOne);
    bool transferRdramToSp(std::uint32_t lengthControl);
    bool transferSpToRdram(std::uint32_t lengthControl);
    bool readImage(const std::vector<std::uint8_t> &image,
                   std::uint64_t offset, std::uint32_t &value) const;

    std::vector<std::uint8_t> rdram_;
    std::vector<std::uint8_t> cartridge_;
    std::vector<std::uint8_t> bootRom_;
    std::vector<std::uint8_t> pifRam_;
    std::vector<std::uint8_t> spDmem_;
    std::vector<std::uint8_t> spImem_;
    std::vector<std::uint8_t> saveRam_;
    Pif pif_;
    std::uint32_t siDramAddress_ = 0;
    std::uint32_t siStatus_ = 0;
    std::uint32_t miInterrupts_ = 0;
    std::uint32_t miInterruptMask_ = 0;
    std::uint32_t piDramAddress_ = 0;
    std::uint32_t piCartAddress_ = 0;
    std::uint32_t piReadLength_ = 0;
    std::uint32_t piWriteLength_ = 0;
    std::uint32_t piStatus_ = 0;
    std::uint32_t spMemAddress_ = 0;
    std::uint32_t spDramAddress_ = 0;
    std::uint32_t spReadLength_ = 0;
    std::uint32_t spWriteLength_ = 0;
    std::uint32_t spStatus_ = 1;
    std::uint32_t spSemaphore_ = 0;
    std::uint32_t spProgramCounter_ = 0;
    std::uint32_t viRegisters_[ViRegisterCount]{};
    std::uint32_t viCycleRemainder_ = 0;
};
