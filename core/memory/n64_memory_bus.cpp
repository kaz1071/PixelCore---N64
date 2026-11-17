#include "core/memory/n64_memory_bus.h"

#include <algorithm>

N64MemoryBus::N64MemoryBus()
    : rdram_(RdramSize, 0), pifRam_(PifRamSize, 0),
      spDmem_(SpMemorySize, 0), spImem_(SpMemorySize, 0),
      saveRam_(SaveRamSize, 0)
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

    if (physical >= SpDmemBase && physical - SpDmemBase < SpMemorySize)
    {
        value = spDmem_[static_cast<std::size_t>(physical - SpDmemBase)];
        return true;
    }
    if (physical >= SpImemBase && physical - SpImemBase < SpMemorySize)
    {
        value = spImem_[static_cast<std::size_t>(physical - SpImemBase)];
        return true;
    }

    if (physical >= SaveRamBase && physical - SaveRamBase < SaveRamSize)
    {
        value = saveRam_[static_cast<std::size_t>(physical - SaveRamBase)];
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
    if (physical >= SpDmemBase && physical - SpDmemBase < SpMemorySize) {
        spDmem_[static_cast<std::size_t>(physical - SpDmemBase)] = value;
        return true;
    }
    if (physical >= SpImemBase && physical - SpImemBase < SpMemorySize) {
        spImem_[static_cast<std::size_t>(physical - SpImemBase)] = value;
        return true;
    }
    if (physical >= SaveRamBase && physical - SaveRamBase < SaveRamSize) {
        saveRam_[static_cast<std::size_t>(physical - SaveRamBase)] = value;
        return true;
    }
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

    if (physical >= SpDmemBase && physical - SpDmemBase <= SpMemorySize - 4)
        return readImage(spDmem_, physical - SpDmemBase, value);
    if (physical >= SpImemBase && physical - SpImemBase <= SpMemorySize - 4)
        return readImage(spImem_, physical - SpImemBase, value);

    if (physical >= SaveRamBase && physical - SaveRamBase <= SaveRamSize - 4)
        return readImage(saveRam_, physical - SaveRamBase, value);

    if (physical >= PifRamBase && physical - PifRamBase <= PifRamSize - 4)
        return readImage(pifRam_, physical - PifRamBase, value);

    if (physical == SpProgramCounter) { value = spProgramCounter_; return true; }
    if (physical == SpMemoryAddress) { value = spMemAddress_; return true; }
    if (physical == SpDramAddress) { value = spDramAddress_; return true; }
    if (physical == SpReadLength) { value = spReadLength_; return true; }
    if (physical == SpWriteLength) { value = spWriteLength_; return true; }
    if (physical == SpStatus) { value = spStatus_; return true; }
    if (physical == SpDmaBusy) { value = 0; return true; }
    if (physical == SpSemaphore) { value = spSemaphore_; spSemaphore_ = 1; return true; }

    if (physical == PiDramAddress) { value = piDramAddress_; return true; }
    if (physical == PiCartAddress) { value = piCartAddress_; return true; }
    if (physical == PiReadLength) { value = piReadLength_; return true; }
    if (physical == PiWriteLength) { value = piWriteLength_; return true; }
    if (physical == PiStatus) { value = piStatus_; return true; }

    if (physical == SiDramAddress) { value = siDramAddress_; return true; }
    if (physical == SiPifAddressRead || physical == SiPifAddressWrite) { value = PifRamBase; return true; }
    if (physical == SiStatus) { value = siStatus_; return true; }
    if (physical == MiVersion) { value = 0x02020102U; return true; }
    if (physical == MiInterrupt) { value = miInterrupts_; return true; }
    if (physical == MiInterruptMask) { value = miInterruptMask_; return true; }
    if (physical >= ViRegisterBase && physical <= ViYScale &&
        ((physical - ViRegisterBase) & 3U) == 0) {
        value = viRegisters_[(physical - ViRegisterBase) / 4];
        return true;
    }

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
    if (physical == SpProgramCounter) {
        spProgramCounter_ = value & 0xFFCU;
        return true;
    }
    if (physical == SpMemoryAddress) {
        spMemAddress_ = value & 0x1FFFU;
        return true;
    }
    if (physical == SpDramAddress) {
        spDramAddress_ = value & 0x00FFFFFFU;
        return true;
    }
    if (physical == SpReadLength) {
        spReadLength_ = value;
        return transferRdramToSp(value);
    }
    if (physical == SpWriteLength) {
        spWriteLength_ = value;
        return transferSpToRdram(value);
    }
    if (physical == SpStatus) {
        if (value & 0x00000001U) spStatus_ &= ~0x01U; // Clear HALT
        if (value & 0x00000002U) spStatus_ |= 0x01U;  // Set HALT
        if (value & 0x00000004U) spStatus_ &= ~0x02U; // Clear BROKE
        return true;
    }
    if (physical == SpSemaphore) {
        spSemaphore_ = 0;
        return true;
    }
    if (physical == PiDramAddress) {
        piDramAddress_ = value & 0x00FFFFFFU;
        return true;
    }
    if (physical == PiCartAddress) {
        piCartAddress_ = value;
        return true;
    }
    if (physical == PiReadLength) {
        piReadLength_ = value & 0x00FFFFFFU;
        return transferRdramToSaveRam(piReadLength_);
    }
    if (physical == PiWriteLength) {
        piWriteLength_ = value & 0x00FFFFFFU;
        return transferCartridgeToRdram(piWriteLength_);
    }
    if (physical == PiStatus) {
        if (value & 0x01U)
            piStatus_ &= ~0x04U;
        if (value & 0x02U) {
            piStatus_ &= ~0x08U;
            miInterrupts_ &= ~0x10U;
        }
        return true;
    }
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
    if (physical >= ViRegisterBase && physical <= ViYScale &&
        ((physical - ViRegisterBase) & 3U) == 0) {
        const std::size_t index = static_cast<std::size_t>((physical - ViRegisterBase) / 4);
        if (index == 3) viRegisters_[index] = value & 0x3FFU;
        else if (index == 4) miInterrupts_ &= ~0x08U; // CURRENT write acknowledges VI.
        else viRegisters_[index] = value;
        return true;
    }
    if (physical >= SpDmemBase && physical - SpDmemBase <= SpMemorySize - 4) {
        const std::size_t index = static_cast<std::size_t>(physical - SpDmemBase);
        spDmem_[index] = static_cast<std::uint8_t>(value >> 24);
        spDmem_[index + 1] = static_cast<std::uint8_t>(value >> 16);
        spDmem_[index + 2] = static_cast<std::uint8_t>(value >> 8);
        spDmem_[index + 3] = static_cast<std::uint8_t>(value);
        return true;
    }
    if (physical >= SpImemBase && physical - SpImemBase <= SpMemorySize - 4) {
        const std::size_t index = static_cast<std::size_t>(physical - SpImemBase);
        spImem_[index] = static_cast<std::uint8_t>(value >> 24);
        spImem_[index + 1] = static_cast<std::uint8_t>(value >> 16);
        spImem_[index + 2] = static_cast<std::uint8_t>(value >> 8);
        spImem_[index + 3] = static_cast<std::uint8_t>(value);
        return true;
    }
    if (physical >= SaveRamBase && physical - SaveRamBase <= SaveRamSize - 4) {
        const std::size_t index = static_cast<std::size_t>(physical - SaveRamBase);
        saveRam_[index] = static_cast<std::uint8_t>(value >> 24);
        saveRam_[index + 1] = static_cast<std::uint8_t>(value >> 16);
        saveRam_[index + 2] = static_cast<std::uint8_t>(value >> 8);
        saveRam_[index + 3] = static_cast<std::uint8_t>(value);
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

void N64MemoryBus::advanceViCycles(std::uint32_t cpuCycles)
{
    // Approximate NTSC R4300i clock divided by 60 Hz and 525 scanlines.
    // CPU instruction timing is currently one cycle per successful step;
    // this is a deterministic early scheduler, not cycle-accurate hardware.
    constexpr std::uint32_t CpuCyclesPerScanline = 29762;
    const std::uint64_t accumulated = static_cast<std::uint64_t>(viCycleRemainder_) + cpuCycles;
    const std::uint32_t scanlines = static_cast<std::uint32_t>(accumulated / CpuCyclesPerScanline);
    viCycleRemainder_ = static_cast<std::uint32_t>(accumulated % CpuCyclesPerScanline);
    if (scanlines != 0)
        advanceViScanlines(scanlines);
}

void N64MemoryBus::advanceViScanlines(std::uint32_t scanlines)
{
    // VI_V_SYNC specifies the number of scanlines in a frame. A zero value
    // is treated as the NTSC default until software programs the register.
    const std::uint32_t frameLines = viRegisters_[6] == 0 ? 525U : viRegisters_[6];
    std::uint32_t current = viRegisters_[4] % frameLines;
    const std::uint32_t compare = viRegisters_[3] & 0x3FFU;

    while (scanlines-- != 0)
    {
        current = (current + 1U) % frameLines;
        if (current == compare)
            miInterrupts_ |= 0x08U;
    }

    viRegisters_[4] = current;
}

bool N64MemoryBus::renderFramebuffer(std::uint32_t width, std::uint32_t height,
                                      std::vector<std::uint32_t> &pixels) const
{
    if (width == 0 || height == 0 || width > viRegisters_[2]) return false;
    const std::uint32_t format = viRegisters_[0] & 3U;
    const std::size_t bytesPerPixel = format == 2 ? 2 : format == 3 ? 4 : 0;
    if (bytesPerPixel == 0) return false;
    const std::uint64_t origin = viRegisters_[1] & 0x00FFFFFFU;
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(width) * bytesPerPixel;
    if (rowBytes == 0 || height > rdram_.size() / rowBytes) return false;
    const std::uint64_t totalBytes = rowBytes * height;
    if (origin > rdram_.size() || totalBytes > rdram_.size() - origin) return false;
    pixels.resize(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t offset = static_cast<std::size_t>(
                origin + static_cast<std::uint64_t>(y) * rowBytes +
                static_cast<std::uint64_t>(x) * bytesPerPixel);
            std::uint8_t r, g, b, a;
            if (format == 2) {
                const std::uint16_t p = static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(rdram_[offset]) << 8) | rdram_[offset + 1]);
                r = static_cast<std::uint8_t>(((p >> 11) & 31U) * 255U / 31U);
                g = static_cast<std::uint8_t>(((p >> 6) & 31U) * 255U / 31U);
                b = static_cast<std::uint8_t>(((p >> 1) & 31U) * 255U / 31U);
                a = (p & 1U) ? 255 : 0;
            } else {
                r = rdram_[offset]; g = rdram_[offset + 1];
                b = rdram_[offset + 2]; a = rdram_[offset + 3];
            }
            pixels[static_cast<std::size_t>(y) * width + x] =
                (static_cast<std::uint32_t>(a) << 24) |
                (static_cast<std::uint32_t>(r) << 16) |
                (static_cast<std::uint32_t>(g) << 8) | b;
        }
    }
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
    std::fill(spDmem_.begin(), spDmem_.end(), 0);
    std::fill(spImem_.begin(), spImem_.end(), 0);
}

void N64MemoryBus::clearPifRam()
{
    std::fill(pifRam_.begin(), pifRam_.end(), 0);
    siDramAddress_ = 0;
    siStatus_ = 0;
    miInterrupts_ = 0;
    miInterruptMask_ = 0;
    piDramAddress_ = 0;
    piCartAddress_ = 0;
    piReadLength_ = 0;
    piWriteLength_ = 0;
    piStatus_ = 0;
    spMemAddress_ = 0;
    spDramAddress_ = 0;
    spReadLength_ = 0;
    spWriteLength_ = 0;
    spStatus_ = 1;
    spSemaphore_ = 0;
    spProgramCounter_ = 0;
    std::fill(viRegisters_, viRegisters_ + ViRegisterCount, 0);
    viCycleRemainder_ = 0;
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

bool N64MemoryBus::transferRdramToSp(std::uint32_t lengthControl)
{
    const std::uint32_t length = (lengthControl & 0x0FFFU) + 1;
    const std::uint32_t count = ((lengthControl >> 12) & 0xFFU) + 1;
    const std::uint32_t skip = (lengthControl >> 20) & 0x0FFFU;
    std::uint64_t dram = spDramAddress_;
    std::uint32_t spAddress = spMemAddress_;

    for (std::uint32_t block = 0; block < count; ++block)
    {
        if (dram > rdram_.size() || length > rdram_.size() - dram)
            return false;

        std::vector<std::uint8_t> &spMemory =
            (spAddress & 0x1000U) ? spImem_ : spDmem_;
        const std::size_t spOffset = spAddress & 0x0FFFU;
        for (std::uint32_t i = 0; i < length; ++i)
            spMemory[(spOffset + i) & 0x0FFFU] = rdram_[static_cast<std::size_t>(dram + i)];

        dram += length + skip;
        spAddress = (spAddress & 0x1000U) | ((spAddress + length) & 0x0FFFU);
    }

    spDramAddress_ = static_cast<std::uint32_t>(dram & 0x00FFFFFFU);
    spMemAddress_ = spAddress;
    return true;
}

bool N64MemoryBus::transferSpToRdram(std::uint32_t lengthControl)
{
    const std::uint32_t length = (lengthControl & 0x0FFFU) + 1;
    const std::uint32_t count = ((lengthControl >> 12) & 0xFFU) + 1;
    const std::uint32_t skip = (lengthControl >> 20) & 0x0FFFU;
    std::uint64_t dram = spDramAddress_;
    std::uint32_t spAddress = spMemAddress_;

    for (std::uint32_t block = 0; block < count; ++block)
    {
        if (dram > rdram_.size() || length > rdram_.size() - dram)
            return false;

        const std::vector<std::uint8_t> &spMemory =
            (spAddress & 0x1000U) ? spImem_ : spDmem_;
        const std::size_t spOffset = spAddress & 0x0FFFU;
        for (std::uint32_t i = 0; i < length; ++i)
            rdram_[static_cast<std::size_t>(dram + i)] = spMemory[(spOffset + i) & 0x0FFFU];

        dram += length + skip;
        spAddress = (spAddress & 0x1000U) | ((spAddress + length) & 0x0FFFU);
    }

    spDramAddress_ = static_cast<std::uint32_t>(dram & 0x00FFFFFFU);
    spMemAddress_ = spAddress;
    return true;
}

bool N64MemoryBus::transferCartridgeToRdram(std::uint32_t lengthMinusOne)
{
    const std::uint64_t length = static_cast<std::uint64_t>(lengthMinusOne) + 1;
    if (piDramAddress_ > rdram_.size() ||
        length > rdram_.size() - piDramAddress_ ||
        piCartAddress_ < CartridgeBase ||
        piCartAddress_ - CartridgeBase > cartridge_.size() ||
        length > cartridge_.size() - static_cast<std::size_t>(piCartAddress_ - CartridgeBase))
    {
        piStatus_ = 0x04;
        return false;
    }

    piStatus_ = 0x01;
    std::copy_n(cartridge_.begin() + (piCartAddress_ - CartridgeBase),
                static_cast<std::size_t>(length),
                rdram_.begin() + piDramAddress_);
    piDramAddress_ += static_cast<std::uint32_t>(length);
    piCartAddress_ += static_cast<std::uint32_t>(length);
    piStatus_ = 0x08;
    miInterrupts_ |= 0x10U;
    return true;
}

bool N64MemoryBus::transferRdramToSaveRam(std::uint32_t lengthMinusOne)
{
    const std::uint64_t length = static_cast<std::uint64_t>(lengthMinusOne) + 1;
    if (piDramAddress_ > rdram_.size() ||
        length > rdram_.size() - piDramAddress_ ||
        piCartAddress_ < SaveRamBase ||
        piCartAddress_ - SaveRamBase > saveRam_.size() ||
        length > saveRam_.size() - static_cast<std::size_t>(piCartAddress_ - SaveRamBase))
    {
        piStatus_ = 0x04;
        return false;
    }

    piStatus_ = 0x01;
    std::copy_n(rdram_.begin() + piDramAddress_,
                static_cast<std::size_t>(length),
                saveRam_.begin() + (piCartAddress_ - SaveRamBase));
    piDramAddress_ += static_cast<std::uint32_t>(length);
    piCartAddress_ += static_cast<std::uint32_t>(length);
    piStatus_ = 0x08;
    miInterrupts_ |= 0x10U;
    return true;
}

std::uint32_t N64MemoryBus::spProgramCounter() const
{
    return spProgramCounter_;
}

void N64MemoryBus::setSpProgramCounter(std::uint32_t address)
{
    spProgramCounter_ = address & 0xFFCU;
}

bool N64MemoryBus::spHalted() const
{
    return (spStatus_ & 0x01U) != 0;
}

void N64MemoryBus::setSpHalted(bool halted)
{
    if (halted) spStatus_ |= 0x01U;
    else spStatus_ &= ~0x01U;
}

void N64MemoryBus::setSpBroke(bool broke)
{
    if (broke) spStatus_ |= 0x02U;
    else spStatus_ &= ~0x02U;
}

bool N64MemoryBus::interruptPending() const
{
    return (miInterrupts_ & miInterruptMask_) != 0;
}

Pif &N64MemoryBus::pif() { return pif_; }
const Pif &N64MemoryBus::pif() const { return pif_; }

std::size_t N64MemoryBus::cartridgeSize() const { return cartridge_.size(); }
std::size_t N64MemoryBus::bootRomSize() const { return bootRom_.size(); }
