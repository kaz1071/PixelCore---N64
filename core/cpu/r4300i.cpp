#include "core/cpu/r4300i.h"

#include <algorithm>

#include <cstdint>
#include <limits>


bool N64Memory::read8(std::uint64_t address, std::uint8_t &value)
{
    std::uint32_t word = 0;
    if (!read32(address & ~3ULL, word)) return false;
    const unsigned int shift = static_cast<unsigned int>((3 - (address & 3)) * 8);
    value = static_cast<std::uint8_t>(word >> shift);
    return true;
}

bool N64Memory::read16(std::uint64_t address, std::uint16_t &value)
{
    if (address & 1) return false;
    std::uint8_t high = 0, low = 0;
    if (!read8(address, high) || !read8(address + 1, low)) return false;
    value = static_cast<std::uint16_t>((high << 8) | low);
    return true;
}

bool N64Memory::read64(std::uint64_t address, std::uint64_t &value)
{
    if (address & 7) return false;
    std::uint32_t high = 0, low = 0;
    if (!read32(address, high) || !read32(address + 4, low)) return false;
    value = (static_cast<std::uint64_t>(high) << 32) | low;
    return true;
}

bool N64Memory::write8(std::uint64_t address, std::uint8_t value)
{
    const std::uint64_t aligned = address & ~3ULL;
    std::uint32_t word = 0;
    if (!read32(aligned, word)) return false;
    const unsigned int shift = static_cast<unsigned int>((3 - (address & 3)) * 8);
    const std::uint32_t mask = 0xFFU << shift;
    word = (word & ~mask) | (static_cast<std::uint32_t>(value) << shift);
    return write32(aligned, word);
}

bool N64Memory::write16(std::uint64_t address, std::uint16_t value)
{
    if (address & 1) return false;
    return write8(address, static_cast<std::uint8_t>(value >> 8)) &&
           write8(address + 1, static_cast<std::uint8_t>(value));
}

bool N64Memory::write64(std::uint64_t address, std::uint64_t value)
{
    if (address & 7) return false;
    return write32(address, static_cast<std::uint32_t>(value >> 32)) &&
           write32(address + 4, static_cast<std::uint32_t>(value));
}

bool N64Memory::interruptPending() const { return false; }

bool R4300i::translateAddress(std::uint64_t virtualAddress, bool isStore,
                              std::uint64_t &physicalAddress,
                              std::uint64_t instructionAddress)
{
    const std::uint32_t address = static_cast<std::uint32_t>(virtualAddress);
    const std::uint64_t status = cp0Registers_[Status];
    const unsigned int mode = static_cast<unsigned int>((status >> 3) & 0x3);
    const bool kernelMode = (status & 0x6) != 0 || mode == 0;
    const bool errorLevel = (status & 0x4) != 0;
    const auto raiseTlbFault = [&](unsigned int exceptionCode) {
        const std::uint64_t asid = cp0Registers_[EntryHi] & 0xFFULL;
        cp0Registers_[EntryHi] =
            (static_cast<std::uint64_t>(address) & ~0x1FFFULL) | asid;
        cp0Registers_[Context] =
            (cp0Registers_[Context] & 0xFF800000ULL) |
            ((static_cast<std::uint64_t>(address) >> 9) & 0x007FFFF0ULL);
        raiseException(exceptionCode, instructionAddress);
        cp0Registers_[BadVAddr] = virtualAddress;
        return false;
    };

    // The R4300i's ERL mode provides a direct-mapped kuseg window. This is
    // also useful immediately after reset, before the OS installs its TLB.
    if (errorLevel && address < 0x80000000U) {
        physicalAddress = address & 0x1FFFFFFFU;
        return true;
    }

    if (address >= 0x80000000U && address < 0xC0000000U) {
        if (!kernelMode) {
            raiseException(isStore ? 5 : 4, instructionAddress, true);
            cp0Registers_[BadVAddr] = virtualAddress;
            return false;
        }
        physicalAddress = address & 0x1FFFFFFFU;
        return true;
    }

    // KUSEG and KSEG2/3 are translated through the 32-entry TLB.
    for (const auto &entry : tlb_) {
        const std::uint64_t pageMask = entry.pageMask & 0x01FFE000ULL;
        const std::uint64_t pairMask = pageMask | 0x1FFFULL;
        const std::uint64_t vpnMask = ~pairMask;
        if ((entry.entryHi & vpnMask) !=
            (static_cast<std::uint64_t>(address) & vpnMask))
            continue;

        const bool global = (entry.entryLo0 & 1ULL) && (entry.entryLo1 & 1ULL);
        const std::uint64_t asid = cp0Registers_[EntryHi] & 0xFFULL;
        if (!global && (entry.entryHi & 0xFFULL) != asid)
            continue;

        const std::uint64_t pageSize = (pairMask + 1) / 2;
        const bool oddPage = (address & pageSize) != 0;
        const std::uint64_t entryLo = oddPage ? entry.entryLo1 : entry.entryLo0;
        if ((entryLo & 2ULL) == 0)
            return raiseTlbFault(isStore ? 3 : 2);
        if (isStore && (entryLo & 4ULL) == 0)
            return raiseTlbFault(3);

        const std::uint64_t offsetMask = pageSize - 1;
        const std::uint64_t physicalBase =
            ((entryLo >> 6) << 12) & ~offsetMask;
        physicalAddress = physicalBase | (address & offsetMask);
        return true;
    }

    return raiseTlbFault(isStore ? 3 : 2);
}

namespace {
std::int64_t signExtend16(std::uint16_t value)
{
    return static_cast<std::int16_t>(value);
}

std::uint64_t signExtend32(std::uint32_t value)
{
    return static_cast<std::uint64_t>(
        static_cast<std::int64_t>(static_cast<std::int32_t>(value)));
}

std::uint32_t low32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value);
}

bool addOverflows32(std::uint32_t a, std::uint32_t b, std::uint32_t result)
{
    return ((~(a ^ b) & (a ^ result)) & 0x80000000U) != 0;
}

bool subOverflows32(std::uint32_t a, std::uint32_t b, std::uint32_t result)
{
    return (((a ^ b) & (a ^ result)) & 0x80000000U) != 0;
}

bool addOverflows64(std::uint64_t a, std::uint64_t b, std::uint64_t result)
{
    return ((~(a ^ b) & (a ^ result)) >> 63) != 0;
}

bool subOverflows64(std::uint64_t a, std::uint64_t b, std::uint64_t result)
{
    return (((a ^ b) & (a ^ result)) >> 63) != 0;
}

struct UInt128Product {
    std::uint64_t high;
    std::uint64_t low;
};

UInt128Product multiplyUnsigned64(std::uint64_t a, std::uint64_t b)
{
    const std::uint64_t aLow = static_cast<std::uint32_t>(a);
    const std::uint64_t aHigh = a >> 32;
    const std::uint64_t bLow = static_cast<std::uint32_t>(b);
    const std::uint64_t bHigh = b >> 32;

    const std::uint64_t lowProduct = aLow * bLow;
    const std::uint64_t middleOne = aHigh * bLow + (lowProduct >> 32);
    const std::uint64_t middleLow = static_cast<std::uint32_t>(middleOne);
    const std::uint64_t middleHigh = middleOne >> 32;
    const std::uint64_t middleTwo = aLow * bHigh + middleLow;

    return {
        aHigh * bHigh + middleHigh + (middleTwo >> 32),
        (middleTwo << 32) | static_cast<std::uint32_t>(lowProduct)
    };
}
}

void R4300i::reset()
{
    registers_.fill(0);
    pc_ = ResetAddress;
    nextPc_ = ResetAddress + 4;
    hi_ = 0;
    lo_ = 0;
    cp0Registers_.fill(0);
    cp0Registers_[PRId] = 0x00000B00; // R4300-family processor ID placeholder.
    cp0Registers_[Status] = 0x00400004; // BEV set; ERL set after reset.
    cp0Registers_[Random] = 31;
    nextInstructionIsDelaySlot_ = false;
    currentInstructionInDelaySlot_ = false;
    llBit_ = false;
    llAddress_ = 0;
    tlb_.fill({});
    pcHistory_.fill(0);
    pcHistoryNext_ = 0;
    pcHistorySize_ = 0;
}

void R4300i::raiseException(unsigned int exceptionCode, std::uint64_t faultAddress,
                            bool hasBadAddress)
{
    llBit_ = false; // Exceptions cancel an outstanding load-linked reservation.
    std::uint64_t &status = cp0Registers_[Status];
    std::uint64_t &cause = cp0Registers_[Cause];
    const bool wasExceptionLevel = (status & 0x2) != 0;
    if (!wasExceptionLevel) {
        cp0Registers_[EPC] = currentInstructionInDelaySlot_ ? faultAddress - 4 : faultAddress;
        if (currentInstructionInDelaySlot_)
            cause |= (1ULL << 31);
        else
            cause &= ~(1ULL << 31);
    }
    cause = (cause & ~0x7CULL) | (static_cast<std::uint64_t>(exceptionCode & 0x1F) << 2);
    if (hasBadAddress)
        cp0Registers_[BadVAddr] = faultAddress;
    const bool tlbRefill = (exceptionCode == 2 || exceptionCode == 3) &&
                           !wasExceptionLevel;
    status |= 0x2; // EXL
    const std::uint64_t vector = tlbRefill
        ? ((status & (1ULL << 22)) ? BootstrapTlbRefillExceptionVector
                                   : TlbRefillExceptionVector)
        : ((status & (1ULL << 22)) ? BootstrapExceptionVector
                                   : GeneralExceptionVector);
    pc_ = vector;
    nextPc_ = vector + 4;
    nextInstructionIsDelaySlot_ = false;
}

std::vector<std::uint64_t> R4300i::recentPcHistory() const
{
    std::vector<std::uint64_t> history;
    history.reserve(pcHistorySize_);
    const std::size_t first = (pcHistoryNext_ + PcHistoryCapacity - pcHistorySize_) % PcHistoryCapacity;
    for (std::size_t i = 0; i < pcHistorySize_; ++i)
        history.push_back(pcHistory_[(first + i) % PcHistoryCapacity]);
    return history;
}

void R4300i::setPc(std::uint64_t address)
{
    pc_ = address;
    nextPc_ = address + 4;
    nextInstructionIsDelaySlot_ = false;
    currentInstructionInDelaySlot_ = false;
}

bool R4300i::step(N64Memory &memory, std::string &error)
{
    pcHistory_[pcHistoryNext_] = pc_;
    pcHistoryNext_ = (pcHistoryNext_ + 1) % PcHistoryCapacity;
    pcHistorySize_ = std::min(pcHistorySize_ + 1, PcHistoryCapacity);

    constexpr std::uint64_t ExternalInterruptBit = 1ULL << 10;
    std::uint64_t &cause = cp0Registers_[Cause];
    if (memory.interruptPending())
        cause |= ExternalInterruptBit;
    else
        cause &= ~ExternalInterruptBit;

    const std::uint64_t status = cp0Registers_[Status];
    const bool interruptsEnabled = (status & 0x1) != 0 &&
                                   (status & 0x6) == 0 &&
                                   (status & cause & 0xFF00) != 0;
    if (interruptsEnabled && !nextInstructionIsDelaySlot_)
    {
        currentInstructionInDelaySlot_ = false;
        raiseException(0, pc_);
        error.clear();
        return true;
    }

    std::uint32_t instruction = 0;
    const std::uint64_t instructionAddress = pc_;

    std::uint64_t physicalInstructionAddress = 0;
    if ((instructionAddress & 3) != 0) {
        currentInstructionInDelaySlot_ = nextInstructionIsDelaySlot_;
        raiseException(4, instructionAddress, true);
        error.clear();
        return true;
    }
    if (!translateAddress(instructionAddress, false, physicalInstructionAddress,
                          instructionAddress)) {
        currentInstructionInDelaySlot_ = nextInstructionIsDelaySlot_;
        error.clear();
        return true;
    }
    if (!memory.read32(physicalInstructionAddress, instruction)) {
        currentInstructionInDelaySlot_ = nextInstructionIsDelaySlot_;
        raiseException(4, instructionAddress, true);
        error.clear();
        return true;
    }

    // Advance before execution so a taken branch still executes its delay slot.
    currentInstructionInDelaySlot_ = nextInstructionIsDelaySlot_;
    nextInstructionIsDelaySlot_ = false;
    pc_ = nextPc_;
    nextPc_ += 4;
    ++cp0Registers_[Count];
    if (static_cast<std::uint32_t>(cp0Registers_[Count]) ==
        static_cast<std::uint32_t>(cp0Registers_[Compare]))
        cp0Registers_[Cause] |= (1ULL << 15); // Timer interrupt pending (IP7).

    if (!execute(instruction, instructionAddress, memory, error)) {
        error.clear();
        return false;
    }

    // Random cycles through [31:Wired], wrapping to 31 at the Wired floor.
    const std::uint64_t wired = cp0Registers_[Wired] & 0x1FULL;
    const std::uint64_t random = cp0Registers_[Random] & 0x1FULL;
    cp0Registers_[Random] = (random <= wired || random > 31) ? 31 : random - 1;

    registers_[0] = 0;
    return true;
}

bool R4300i::execute(std::uint32_t instruction, std::uint64_t instructionAddress,
                     N64Memory &memory, std::string &error)
{
    (void)error; // Execution failures are currently reported through CPU exceptions.
    const unsigned int opcode = instruction >> 26;
    const unsigned int rs = (instruction >> 21) & 0x1F;
    const unsigned int rt = (instruction >> 16) & 0x1F;
    const unsigned int rd = (instruction >> 11) & 0x1F;
    const unsigned int shift = (instruction >> 6) & 0x1F;
    const unsigned int function = instruction & 0x3F;
    const std::uint16_t immediate = instruction & 0xFFFF;
    const std::int64_t offset = signExtend16(immediate);
    const std::uint64_t a = registers_[rs];
    const std::uint64_t b = registers_[rt];

    switch (opcode) {
    case 0x00: // SPECIAL
        switch (function) {
        case 0x00: registers_[rd] = signExtend32(low32(b) << shift); break; // SLL
        case 0x02: registers_[rd] = signExtend32(low32(b) >> shift); break; // SRL
        case 0x03: registers_[rd] = signExtend32(static_cast<std::uint32_t>(
                       static_cast<std::int32_t>(low32(b)) >> shift)); break; // SRA
        case 0x04: registers_[rd] = signExtend32(low32(b) << (a & 0x1F)); break; // SLLV
        case 0x06: registers_[rd] = signExtend32(low32(b) >> (a & 0x1F)); break; // SRLV
        case 0x07: registers_[rd] = signExtend32(static_cast<std::uint32_t>(
                       static_cast<std::int32_t>(low32(b)) >> (a & 0x1F))); break; // SRAV
        case 0x08: nextPc_ = a; nextInstructionIsDelaySlot_ = true; break; // JR
        case 0x09: registers_[rd == 0 ? 31 : rd] = instructionAddress + 8; nextPc_ = a; nextInstructionIsDelaySlot_ = true; break; // JALR
        case 0x10: registers_[rd] = hi_; break; // MFHI
        case 0x11: hi_ = a; break; // MTHI
        case 0x12: registers_[rd] = lo_; break; // MFLO
        case 0x13: lo_ = a; break; // MTLO
        case 0x18: { // MULT
            const std::int64_t product = static_cast<std::int64_t>(
                static_cast<std::int32_t>(low32(a))) *
                static_cast<std::int64_t>(static_cast<std::int32_t>(low32(b)));
            const std::uint64_t bits = static_cast<std::uint64_t>(product);
            lo_ = signExtend32(low32(bits));
            hi_ = signExtend32(static_cast<std::uint32_t>(bits >> 32));
            break;
        }
        case 0x19: { // MULTU
            const std::uint64_t product = static_cast<std::uint64_t>(low32(a)) *
                                          static_cast<std::uint64_t>(low32(b));
            lo_ = signExtend32(low32(product));
            hi_ = signExtend32(static_cast<std::uint32_t>(product >> 32));
            break;
        }
        case 0x1A: { // DIV
            const std::int32_t dividend = static_cast<std::int32_t>(low32(a));
            const std::int32_t divisor = static_cast<std::int32_t>(low32(b));
            if (divisor != 0) {
                if (dividend == std::numeric_limits<std::int32_t>::min() && divisor == -1) {
                    lo_ = signExtend32(0x80000000U);
                    hi_ = 0;
                } else {
                    lo_ = signExtend32(static_cast<std::uint32_t>(dividend / divisor));
                    hi_ = signExtend32(static_cast<std::uint32_t>(dividend % divisor));
                }
            }
            break;
        }
        case 0x1B: { // DIVU
            const std::uint32_t dividend = low32(a);
            const std::uint32_t divisor = low32(b);
            if (divisor != 0) {
                lo_ = signExtend32(dividend / divisor);
                hi_ = signExtend32(dividend % divisor);
            }
            break;
        }

        case 0x1C: { // DMULT (signed 64-bit multiply)
            UInt128Product product = multiplyUnsigned64(a, b);
            if (static_cast<std::int64_t>(a) < 0)
                product.high -= b;
            if (static_cast<std::int64_t>(b) < 0)
                product.high -= a;
            lo_ = product.low;
            hi_ = product.high;
            break;
        }
        case 0x1D: { // DMULTU (unsigned 64-bit multiply)
            const UInt128Product product = multiplyUnsigned64(a, b);
            lo_ = product.low;
            hi_ = product.high;
            break;
        }
        case 0x1E: { // DDIV (signed 64-bit divide)
            const std::int64_t dividend = static_cast<std::int64_t>(a);
            const std::int64_t divisor = static_cast<std::int64_t>(b);
            if (divisor != 0) {
                if (dividend == std::numeric_limits<std::int64_t>::min() && divisor == -1) {
                    lo_ = 0x8000000000000000ULL;
                    hi_ = 0;
                } else {
                    lo_ = static_cast<std::uint64_t>(dividend / divisor);
                    hi_ = static_cast<std::uint64_t>(dividend % divisor);
                }
            }
            break;
        }
        case 0x1F: { // DDIVU (unsigned 64-bit divide)
            if (b != 0) {
                lo_ = a / b;
                hi_ = a % b;
            }
            break;
        }
        case 0x20: { // ADD (signed 32-bit, traps on overflow)
            const std::uint32_t lhs = low32(a), rhs = low32(b);
            const std::uint32_t result = lhs + rhs;
            if (addOverflows32(lhs, rhs, result)) {
                raiseException(12, instructionAddress);
                return true;
            }
            registers_[rd] = signExtend32(result);
            break;
        }
        case 0x21: registers_[rd] = signExtend32(low32(a + b)); break; // ADDU
        case 0x22: { // SUB (signed 32-bit, traps on overflow)
            const std::uint32_t lhs = low32(a), rhs = low32(b);
            const std::uint32_t result = lhs - rhs;
            if (subOverflows32(lhs, rhs, result)) {
                raiseException(12, instructionAddress);
                return true;
            }
            registers_[rd] = signExtend32(result);
            break;
        }
        case 0x23: registers_[rd] = signExtend32(low32(a - b)); break; // SUBU
        case 0x24: registers_[rd] = a & b; break; // AND
        case 0x25: registers_[rd] = a | b; break; // OR
        case 0x26: registers_[rd] = a ^ b; break; // XOR
        case 0x27: registers_[rd] = ~(a | b); break; // NOR
        case 0x2A: registers_[rd] = static_cast<std::int64_t>(a) < static_cast<std::int64_t>(b); break; // SLT
        case 0x2B: registers_[rd] = a < b; break; // SLTU
        case 0x2C: { // DADD (signed 64-bit, traps on overflow)
            const std::uint64_t result = a + b;
            if (addOverflows64(a, b, result)) {
                raiseException(12, instructionAddress);
                return true;
            }
            registers_[rd] = result;
            break;
        }
        case 0x2D: registers_[rd] = a + b; break; // DADDU
        case 0x2E: { // DSUB (signed 64-bit, traps on overflow)
            const std::uint64_t result = a - b;
            if (subOverflows64(a, b, result)) {
                raiseException(12, instructionAddress);
                return true;
            }
            registers_[rd] = result;
            break;
        }
        case 0x2F: registers_[rd] = a - b; break; // DSUBU
        case 0x38: registers_[rd] = b << shift; break; // DSLL
        case 0x3A: registers_[rd] = b >> shift; break; // DSRL
        case 0x3B: registers_[rd] = static_cast<std::uint64_t>(
                       static_cast<std::int64_t>(b) >> shift); break; // DSRA
        case 0x3C: registers_[rd] = b << (shift + 32); break; // DSLL32
        case 0x3E: registers_[rd] = b >> (shift + 32); break; // DSRL32
        case 0x3F: registers_[rd] = static_cast<std::uint64_t>(
                       static_cast<std::int64_t>(b) >> (shift + 32)); break; // DSRA32
        default: raiseException(10, instructionAddress); return true; // Reserved Instruction
        }
        return true;

    case 0x02: // J
    case 0x03: // JAL
        if (opcode == 0x03) registers_[31] = instructionAddress + 8;
        nextPc_ = ((instructionAddress + 4) & 0xFFFFFFFFF0000000ULL) |
                  (static_cast<std::uint64_t>(instruction & 0x03FFFFFF) << 2);
        nextInstructionIsDelaySlot_ = true;
        return true;
    case 0x04: // BEQ
        if (a == b) nextPc_ = pc_ + static_cast<std::uint64_t>(offset * 4);
        nextInstructionIsDelaySlot_ = true;
        return true;
    case 0x05: // BNE
        if (a != b) nextPc_ = pc_ + static_cast<std::uint64_t>(offset * 4);
        nextInstructionIsDelaySlot_ = true;
        return true;
    case 0x06: // BLEZ
        if (rt != 0) break;
        if (static_cast<std::int64_t>(a) <= 0)
            nextPc_ = pc_ + static_cast<std::uint64_t>(offset * 4);
        nextInstructionIsDelaySlot_ = true;
        return true;
    case 0x07: // BGTZ
        if (rt != 0) break;
        if (static_cast<std::int64_t>(a) > 0)
            nextPc_ = pc_ + static_cast<std::uint64_t>(offset * 4);
        nextInstructionIsDelaySlot_ = true;
        return true;
    case 0x01: { // REGIMM conditional branches
        bool takeBranch = false;
        bool link = false;
        switch (rt) {
        case 0x00: takeBranch = static_cast<std::int64_t>(a) < 0; break; // BLTZ
        case 0x01: takeBranch = static_cast<std::int64_t>(a) >= 0; break; // BGEZ
        case 0x10: takeBranch = static_cast<std::int64_t>(a) < 0; link = true; break; // BLTZAL
        case 0x11: takeBranch = static_cast<std::int64_t>(a) >= 0; link = true; break; // BGEZAL
        default: break;
        }
        if (rt != 0x00 && rt != 0x01 && rt != 0x10 && rt != 0x11)
            break;
        if (link) registers_[31] = instructionAddress + 8;
        if (takeBranch)
            nextPc_ = pc_ + static_cast<std::uint64_t>(offset * 4);
        nextInstructionIsDelaySlot_ = true;
        return true;
    }
    case 0x08: { // ADDI (signed 32-bit immediate, traps on overflow)
        const std::uint32_t lhs = low32(a);
        const std::uint32_t rhs = static_cast<std::uint32_t>(offset);
        const std::uint32_t result = lhs + rhs;
        if (addOverflows32(lhs, rhs, result)) {
            raiseException(12, instructionAddress);
            return true;
        }
        registers_[rt] = signExtend32(result);
        return true;
    }
    case 0x09: registers_[rt] = signExtend32(low32(a + offset)); return true; // ADDIU
    case 0x0A: registers_[rt] = static_cast<std::int64_t>(a) < offset; return true; // SLTI
    case 0x0B: registers_[rt] = a < static_cast<std::uint64_t>(offset); return true; // SLTIU
    case 0x0C: registers_[rt] = a & immediate; return true; // ANDI
    case 0x0D: registers_[rt] = a | immediate; return true; // ORI
    case 0x0E: registers_[rt] = a ^ immediate; return true; // XORI
    case 0x0F: registers_[rt] = signExtend32(static_cast<std::uint32_t>(immediate) << 16); return true; // LUI
    case 0x18: { // DADDI (signed 64-bit immediate, traps on overflow)
        const std::uint64_t rhs = static_cast<std::uint64_t>(offset);
        const std::uint64_t result = a + rhs;
        if (addOverflows64(a, rhs, result)) {
            raiseException(12, instructionAddress);
            return true;
        }
        registers_[rt] = result;
        return true;
    }
    case 0x19: registers_[rt] = a + offset; return true; // DADDIU
    case 0x10: // COP0
        if (rs == 0x00) { // MFC0
            registers_[rt] = signExtend32(static_cast<std::uint32_t>(cp0Registers_[rd]));
            return true;
        }
        if (rs == 0x04) { // MTC0
            cp0Registers_[rd] = signExtend32(low32(b));
            if (rd == Wired) {
                cp0Registers_[Wired] &= 0x1FULL;
                cp0Registers_[Random] = 31;
            }
            if (rd == Compare)
                cp0Registers_[Cause] &= ~(1ULL << 15); // Writing Compare acknowledges timer.
            return true;
        }
        if (rs == 0x10 && function == 0x18) { // ERET
            std::uint64_t &status = cp0Registers_[Status];
            if (status & 0x4) { // ERL
                status &= ~0x4ULL;
                pc_ = cp0Registers_[ErrorEPC];
            } else {
                status &= ~0x2ULL; // EXL
                pc_ = cp0Registers_[EPC];
            }
            nextPc_ = pc_ + 4;
            nextInstructionIsDelaySlot_ = false;
            return true;
        }
        if (rs == 0x10) {
            const unsigned int index = static_cast<unsigned int>(cp0Registers_[Index] & 0x1F);
            switch (function) {
            case 0x01: // TLBR: copy indexed TLB entry to CP0 registers.
                cp0Registers_[PageMask] = tlb_[index].pageMask;
                cp0Registers_[EntryHi] = tlb_[index].entryHi;
                cp0Registers_[EntryLo0] = tlb_[index].entryLo0;
                cp0Registers_[EntryLo1] = tlb_[index].entryLo1;
                return true;
            case 0x02: // TLBWI: write indexed TLB entry.
                tlb_[index] = {cp0Registers_[PageMask], cp0Registers_[EntryHi],
                               cp0Registers_[EntryLo0], cp0Registers_[EntryLo1]};
                return true;
            case 0x06: { // TLBWR: write entry selected by Random.
                const unsigned int random = static_cast<unsigned int>(cp0Registers_[Random] & 0x1F);
                tlb_[random] = {cp0Registers_[PageMask], cp0Registers_[EntryHi],
                                cp0Registers_[EntryLo0], cp0Registers_[EntryLo1]};
                return true;
            }
            case 0x08: { // TLBP: probe for matching VPN2 and ASID/global entry.
                cp0Registers_[Index] = 0x80000000ULL;
                const std::uint64_t queryAsid = cp0Registers_[EntryHi] & 0xFFULL;
                for (unsigned int i = 0; i < tlb_.size(); ++i) {
                    const auto &entry = tlb_[i];
                    // PageMask enlarges the TLB page pair. Ignore the VPN
                    // bits covered by the mask when comparing virtual pages.
                    const std::uint64_t vpnMask = ~(entry.pageMask | 0x1FFFULL);
                    const bool vpnMatches =
                        (entry.entryHi & vpnMask) == (cp0Registers_[EntryHi] & vpnMask);
                    const bool global = (entry.entryLo0 & 1ULL) && (entry.entryLo1 & 1ULL);
                    if (vpnMatches &&
                        (global || (entry.entryHi & 0xFFULL) == queryAsid)) {
                        cp0Registers_[Index] = i;
                        break;
                    }
                }
                return true;
            }
            default: break;
            }
        }
        raiseException(11, instructionAddress); // Coprocessor Unusable / unsupported COP0 operation.
        return true;
    case 0x1C: // SPECIAL2 is not part of the base R4300i ISA.
        break;
    case 0x30: { // LL (Load Linked, 32-bit)
        const std::uint64_t address = a + offset;
        std::uint64_t physicalAddress = 0;
        std::uint32_t value = 0;
        if (address & 3) {
            raiseException(4, address, true);
            return true;
        }
        if (!translateAddress(address, false, physicalAddress, instructionAddress))
            return true;
        if (!memory.read32(physicalAddress, value)) {
            raiseException(4, address, true);
            return true;
        }
        registers_[rt] = signExtend32(value);
        llAddress_ = physicalAddress;
        llBit_ = true;
        cp0Registers_[LLAddr] = physicalAddress & ~0xFULL;
        return true;
    }
    case 0x20: // LB
    case 0x24: { // LBU
        const std::uint64_t address = a + offset;
        std::uint64_t physicalAddress = 0;
        std::uint8_t value = 0;
        if (!translateAddress(address, false, physicalAddress, instructionAddress))
            return true;
        if (!memory.read8(physicalAddress, value)) {
            raiseException(4, address, true);
            return true;
        }
        registers_[rt] = opcode == 0x20
            ? static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int8_t>(value)))
            : value;
        return true;
    }
    case 0x21: // LH
    case 0x25: { // LHU
        const std::uint64_t address = a + offset;
        std::uint64_t physicalAddress = 0;
        std::uint16_t value = 0;
        if (address & 1) {
            raiseException(4, address, true);
            return true;
        }
        if (!translateAddress(address, false, physicalAddress, instructionAddress))
            return true;
        if (!memory.read16(physicalAddress, value)) {
            raiseException(4, address, true);
            return true;
        }
        registers_[rt] = opcode == 0x21
            ? static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(value)))
            : value;
        return true;
    }
    case 0x22: // LWL (big-endian)
    case 0x26: { // LWR (big-endian)
        const std::uint64_t address = a + offset;
        std::uint64_t physicalAddress = 0;
        std::uint32_t memoryWord = 0;
        if (!translateAddress(address, false, physicalAddress, instructionAddress))
            return true;
        if (!memory.read32(physicalAddress & ~3ULL, memoryWord)) {
            raiseException(4, address, true);
            return true;
        }
        const unsigned int byteOffset = static_cast<unsigned int>(address & 3);
        if (opcode == 0x22) {
            const unsigned int shift = byteOffset * 8;
            const std::uint32_t preserveMask = byteOffset == 0
                ? 0U : (0xFFFFFFFFU >> ((4 - byteOffset) * 8));
            registers_[rt] = signExtend32((memoryWord << shift) |
                                          (low32(b) & preserveMask));
        } else {
            const unsigned int shift = (3 - byteOffset) * 8;
            const std::uint32_t preserveMask = byteOffset == 3
                ? 0U : (0xFFFFFFFFU << ((byteOffset + 1) * 8));
            registers_[rt] = signExtend32((memoryWord >> shift) |
                                          (low32(b) & preserveMask));
        }
        return true;
    }
    case 0x23: // LW
    case 0x27: { // LWU
        const std::uint64_t address = a + offset;
        std::uint64_t physicalAddress = 0;
        std::uint32_t value = 0;
        if (address & 3) {
            raiseException(4, address, true);
            return true;
        }
        if (!translateAddress(address, false, physicalAddress, instructionAddress))
            return true;
        if (!memory.read32(physicalAddress, value)) {
            raiseException(4, address, true);
            return true;
        }
        registers_[rt] = opcode == 0x23 ? signExtend32(value) : value;
        return true;
    }
    case 0x37: { // LD
        const std::uint64_t address = a + offset;
        std::uint64_t physicalAddress = 0, value = 0;
        if (address & 7) {
            raiseException(4, address, true);
            return true;
        }
        if (!translateAddress(address, false, physicalAddress, instructionAddress))
            return true;
        if (!memory.read64(physicalAddress, value)) {
            raiseException(4, address, true);
            return true;
        }
        registers_[rt] = value;
        return true;
    }
    case 0x28: // SB
    case 0x29: // SH
    case 0x2B: // SW
    case 0x3F: { // SD
        const std::uint64_t address = a + offset;
        llBit_ = false;
        const unsigned int alignment = opcode == 0x29 ? 2 :
                                       opcode == 0x2B ? 4 :
                                       opcode == 0x3F ? 8 : 1;
        if ((address & (alignment - 1)) != 0) {
            raiseException(5, address, true);
            return true;
        }
        std::uint64_t physicalAddress = 0;
        if (!translateAddress(address, true, physicalAddress, instructionAddress))
            return true;
        bool written = false;
        switch (opcode) {
        case 0x28: written = memory.write8(physicalAddress, static_cast<std::uint8_t>(b)); break;
        case 0x29: written = memory.write16(physicalAddress, static_cast<std::uint16_t>(b)); break;
        case 0x2B: written = memory.write32(physicalAddress, low32(b)); break;
        case 0x3F: written = memory.write64(physicalAddress, b); break;
        }
        if (!written) raiseException(5, address, true);
        return true;
    }
    case 0x2A: // SWL (big-endian)
    case 0x2E: { // SWR (big-endian)
        const std::uint64_t address = a + offset;
        llBit_ = false;
        std::uint64_t physicalAddress = 0;
        if (!translateAddress(address, true, physicalAddress, instructionAddress))
            return true;
        const std::uint64_t aligned = physicalAddress & ~3ULL;
        std::uint32_t memoryWord = 0;
        if (!memory.read32(aligned, memoryWord)) {
            raiseException(5, address, true);
            return true;
        }
        const unsigned int byteOffset = static_cast<unsigned int>(address & 3);
        const unsigned int shift = opcode == 0x2A ? byteOffset * 8
                                                   : (3 - byteOffset) * 8;
        const std::uint32_t mask = opcode == 0x2A
            ? (0xFFFFFFFFU >> shift)
            : (0xFFFFFFFFU << shift);
        const std::uint32_t value = opcode == 0x2A
            ? (low32(b) >> shift) : (low32(b) << shift);
        if (!memory.write32(aligned, (memoryWord & ~mask) | (value & mask)))
            raiseException(5, address, true);
        return true;
    }
    case 0x38: { // SC (Store Conditional, 32-bit)
        const std::uint64_t address = a + offset;
        if (address & 3) {
            llBit_ = false;
            raiseException(5, address, true);
            return true;
        }
        std::uint64_t physicalAddress = 0;
        if (!translateAddress(address, true, physicalAddress, instructionAddress)) {
            llBit_ = false;
            return true;
        }
        const bool reservationMatches = llBit_ && llAddress_ == physicalAddress;
        llBit_ = false;
        if (!reservationMatches) {
            registers_[rt] = 0;
            return true;
        }
        if (!memory.write32(physicalAddress, low32(b))) {
            raiseException(5, address, true);
            return true;
        }
        registers_[rt] = 1;
        return true;
    }
    default: break;
    }

    raiseException(10, instructionAddress); // Reserved Instruction.
    return true;
}

std::uint64_t R4300i::reg(unsigned int index) const
{
    return index < registers_.size() ? registers_[index] : 0;
}

void R4300i::setReg(unsigned int index, std::uint64_t value)
{
    if (index > 0 && index < registers_.size()) registers_[index] = value;
}

std::uint64_t R4300i::pc() const { return pc_; }
std::uint64_t R4300i::hi() const { return hi_; }
std::uint64_t R4300i::lo() const { return lo_; }

std::uint64_t R4300i::cp0(unsigned int index) const
{
    return index < cp0Registers_.size() ? cp0Registers_[index] : 0;
}

void R4300i::setCp0(unsigned int index, std::uint64_t value)
{
    if (index < cp0Registers_.size()) cp0Registers_[index] = value;
}
