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
    static constexpr std::size_t PifRamSize = 64;

    N64MemoryBus();

    bool read8(std::uint64_t address, std::uint8_t &value) override;
    bool write8(std::uint64_t address, std::uint8_t value) override;
    bool read32(std::uint64_t address, std::uint32_t &value) override;
    bool write32(std::uint64_t address, std::uint32_t value) override;
    bool interruptPending() const override;

    void loadCartridge(const std::vector<std::uint8_t> &image);
    void loadBootRom(const std::vector<std::uint8_t> &image);
    void clearRdram();
    void clearPifRam();
    Pif &pif();
    const Pif &pif() const;

    std::size_t cartridgeSize() const;
    std::size_t bootRomSize() const;

private:
    static std::uint64_t toPhysicalAddress(std::uint64_t address);
    void processPifControl();
    bool transferRdramToPif();
    bool transferPifToRdram();
    bool transferCartridgeToRdram(std::uint32_t lengthMinusOne);
    bool transferRdramToSaveRam(std::uint32_t lengthMinusOne);
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
};
