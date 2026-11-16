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
    static constexpr std::size_t PifRamSize = 64;

    N64MemoryBus();

    bool read8(std::uint64_t address, std::uint8_t &value) override;
    bool write8(std::uint64_t address, std::uint8_t value) override;
    bool read32(std::uint64_t address, std::uint32_t &value) override;
    bool write32(std::uint64_t address, std::uint32_t value) override;

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
    bool readImage(const std::vector<std::uint8_t> &image,
                   std::uint64_t offset, std::uint32_t &value) const;

    std::vector<std::uint8_t> rdram_;
    std::vector<std::uint8_t> cartridge_;
    std::vector<std::uint8_t> bootRom_;
    std::vector<std::uint8_t> pifRam_;
    Pif pif_;
};
