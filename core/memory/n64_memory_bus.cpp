#include "core/memory/n64_memory_bus.h"

#include <algorithm>

N64MemoryBus::N64MemoryBus()
    : rdram_(RdramSize, 0), pifRam_(PifRamSize, 0)
{
}

std::uint64_t N64MemoryBus::toPhysicalAddress(std::uint64_t address)
{
    // KSEG0 and KSEG1 are direct-mapped aliases of the low 512 MiB.
    if (address >= 0x80000000ULL && address <= 0xBFFFFFFFULL)
        return address & 0x1FFFFFFFULL;
    return address;
}

bool N64MemoryBus::readImage(const std::vector<std::uint8_t> &image,
                             std::uint64_t offset, std::uint32_t &value) const
{
    if (offset > image.size() || image.size() - static_cast<std::size_t>(offset) < 4)
        return false;

    const std::size_t index = static_cast<std::size_t>(offset);
    value = (static_cast<std::uint32_t>(image[index]) << 24) |
            (static_cast<std::uint32_t>(image[index + 1]) << 16) |
            (static_cast<std::uint32_t>(image[index + 2]) << 8) |
             static_cast<std::uint32_t>(image[index + 3]);
    return true;
}

bool N64MemoryBus::read8(std::uint64_t address, std::uint8_t &value)
{
    const std::uint64_t physical = toPhysicalAddress(address);

    if (physical < RdramSize)
    {
        value = rdram_[static_cast<std::size_t>(physical)];
        return true;
    }

    if (physical >= PifRamBase && physical - PifRamBase < PifRamSize)
    {
        value = pifRam_[static_cast<std::size_t>(physical - PifRamBase)];
        return true;
    }

    if (physical >= CartridgeBase)
    {
        const std::uint64_t offset = physical - CartridgeBase;
        if (offset < cartridge_.size())
        {
            value = cartridge_[static_cast<std::size_t>(offset)];
            return true;
        }
    }

    if (physical >= BootRomBase)
    {
        const std::uint64_t offset = physical - BootRomBase;
        if (offset < bootRom_.size())
        {
            value = bootRom_[static_cast<std::size_t>(offset)];
            return true;
        }
    }

    return false;
}

bool N64MemoryBus::write8(std::uint64_t address, std::uint8_t value)
{
    const std::uint64_t physical = toPhysicalAddress(address);
    if (physical >= PifRamBase && physical - PifRamBase < PifRamSize) {
        const std::size_t offset = static_cast<std::size_t>(physical - PifRamBase);
        pifRam_[offset] = value;
        return true;
    }
    if (physical >= RdramSize)
        return false;

    rdram_[static_cast<std::size_t>(physical)] = value;
    return true;
}

bool N64MemoryBus::read32(std::uint64_t address, std::uint32_t &value)
{
    if (address & 3)
        return false;

    const std::uint64_t physical = toPhysicalAddress(address);

    if (physical < RdramSize)
        return readImage(rdram_, physical, value);

    if (physical >= PifRamBase && physical - PifRamBase <= PifRamSize - 4)
        return readImage(pifRam_, physical - PifRamBase, value);

    if (physical == SiDramAddress) { value = siDramAddress_; return true; }
    if (physical == SiPifAddressRead || physical == SiPifAddressWrite) { value = PifRamBase; return true; }
    if (physical == SiStatus) { value = siStatus_; return true; }
    if (physical == MiVersion) { value = 0x02020102U; return true; }
    if (physical == MiInterrupt) { value = miInterrupts_; return true; }
    if (physical == MiInterruptMask) { value = miInterruptMask_; return true; }

    if (physical >= CartridgeBase &&
        physical - CartridgeBase <= cartridge_.size() &&
        cartridge_.size() - static_cast<std::size_t>(physical - CartridgeBase) >= 4)
        return readImage(cartridge_, physical - CartridgeBase, value);

    if (physical >= BootRomBase &&
        physical - BootRomBase <= bootRom_.size() &&
        bootRom_.size() - static_cast<std::size_t>(physical - BootRomBase) >= 4)
        return readImage(bootRom_, physical - BootRomBase, value);

    return false;
}

bool N64MemoryBus::write32(std::uint64_t address, std::uint32_t value)
{
    if (address & 3)
        return false;

    const std::uint64_t physical = toPhysicalAddress(address);
    if (physical == SiDramAddress) {
        siDramAddress_ = value & 0x00FFFFFFU;
        return true;
    }
    if (physical == SiPifAddressRead)
        return transferPifToRdram();
    if (physical == SiPifAddressWrite)
        return transferRdramToPif();
    if (physical == SiStatus) {
        // Any write clears the SI interrupt request.
        siStatus_ &= ~0x1000U;
        miInterrupts_ &= ~0x02U;
        return true;
    }
    if (physical == MiInterruptMask) {
        for (unsigned int bit = 0; bit < 6; ++bit) {
            if (value & (1U << (bit * 2)))
                miInterruptMask_ &= ~(1U << bit);
            if (value & (1U << (bit * 2 + 1)))
                miInterruptMask_ |= (1U << bit);
        }
        return true;
    }
    if (physical >= PifRamBase && physical - PifRamBase <= PifRamSize - 4) {
        const std::size_t index = static_cast<std::size_t>(physical - PifRamBase);
        pifRam_[index] = static_cast<std::uint8_t>(value >> 24);
        pifRam_[index + 1] = static_cast<std::uint8_t>(value >> 16);
        pifRam_[index + 2] = static_cast<std::uint8_t>(value >> 8);
        pifRam_[index + 3] = static_cast<std::uint8_t>(value);
        return true;
    }
    if (physical >= RdramSize)
        return false;

    const std::size_t index = static_cast<std::size_t>(physical);
    rdram_[index] = static_cast<std::uint8_t>(value >> 24);
    rdram_[index + 1] = static_cast<std::uint8_t>(value >> 16);
    rdram_[index + 2] = static_cast<std::uint8_t>(value >> 8);
    rdram_[index + 3] = static_cast<std::uint8_t>(value);
    return true;
}

void N64MemoryBus::loadCartridge(const std::vector<std::uint8_t> &image)
{
    cartridge_ = image;
}

void N64MemoryBus::loadBootRom(const std::vector<std::uint8_t> &image)
{
    bootRom_ = image;
}

void N64MemoryBus::clearRdram()
{
    std::fill(rdram_.begin(), rdram_.end(), 0);
}

void N64MemoryBus::clearPifRam()
{
    std::fill(pifRam_.begin(), pifRam_.end(), 0);
    siDramAddress_ = 0;
    siStatus_ = 0;
    miInterrupts_ = 0;
    miInterruptMask_ = 0;
}

void N64MemoryBus::processPifControl()
{
    pif_.processJoybus(pifRam_);
}

bool N64MemoryBus::transferRdramToPif()
{
    if ((siDramAddress_ & 7U) != 0 ||
        siDramAddress_ > rdram_.size() ||
        rdram_.size() - siDramAddress_ < PifRamSize)
    {
        siStatus_ = 0x08;
        return false;
    }

    siStatus_ = 0x01;
    std::copy_n(rdram_.begin() + siDramAddress_, PifRamSize, pifRam_.begin());
    processPifControl();
    siStatus_ = 0x1000;
    miInterrupts_ |= 0x02U;
    return true;
}

bool N64MemoryBus::transferPifToRdram()
{
    if ((siDramAddress_ & 7U) != 0 ||
        siDramAddress_ > rdram_.size() ||
        rdram_.size() - siDramAddress_ < PifRamSize)
    {
        siStatus_ = 0x08;
        return false;
    }

    siStatus_ = 0x01;
    std::copy(pifRam_.begin(), pifRam_.end(), rdram_.begin() + siDramAddress_);
    siStatus_ = 0x1000;
    miInterrupts_ |= 0x02U;
    return true;
}

bool N64MemoryBus::interruptPending() const
{
    return (miInterrupts_ & miInterruptMask_) != 0;
}

Pif &N64MemoryBus::pif() { return pif_; }
const Pif &N64MemoryBus::pif() const { return pif_; }

std::size_t N64MemoryBus::cartridgeSize() const { return cartridge_.size(); }
std::size_t N64MemoryBus::bootRomSize() const { return bootRom_.size(); }
