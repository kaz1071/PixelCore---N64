#include "core/cpu/r4300i.h"

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
}

void R4300i::raiseException(unsigned int exceptionCode, std::uint64_t faultAddress,
                            bool hasBadAddress)
{
    llBit_ = false; // Exceptions cancel an outstanding load-linked reservation.
    std::uint64_t &status = cp0Registers_[Status];
    std::uint64_t &cause = cp0Registers_[Cause];
    if ((status & 0x2) == 0) {
        cp0Registers_[EPC] = currentInstructionInDelaySlot_ ? faultAddress - 4 : faultAddress;
        if (currentInstructionInDelaySlot_)
            cause |= (1ULL << 31);
        else
            cause &= ~(1ULL << 31);
    }
    cause = (cause & ~0x7CULL) | (static_cast<std::uint64_t>(exceptionCode & 0x1F) << 2);
    if (hasBadAddress)
        cp0Registers_[BadVAddr] = faultAddress;
    status |= 0x2; // EXL
    const std::uint64_t vector = (status & (1ULL << 22))
        ? BootstrapExceptionVector : GeneralExceptionVector;
    pc_ = vector;
    nextPc_ = vector + 4;
    nextInstructionIsDelaySlot_ = false;
}

bool R4300i::step(N64Memory &memory, std::string &error)
{
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

    if (!memory.read32(instructionAddress, instruction)) {
        currentInstructionInDelaySlot_ = nextInstructionIsDelaySlot_;
        raiseException(4, instructionAddress, true); // Address Error on instruction fetch.
        error.clear();
        return true;
    }

    // Advance before execution so a taken branch still executes its delay slot.
    currentInstructionInDelaySlot_ = nextInstructionIsDelaySlot_;
    nextInstructionIsDelaySlot_ = false;
    pc_ = nextPc_;
    nextPc_ += 4;
    ++cp0Registers_[Count];

    if (!execute(instruction, instructionAddress, memory, error)) {
        error.clear();
        return false;
    }

    registers_[0] = 0;
    return true;
}

bool R4300i::execute(std::uint32_t instruction, std::uint64_t instructionAddress,
                     N64Memory &memory, std::string &error)
{
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
            const __int128 product = static_cast<__int128>(static_cast<std::int64_t>(a)) *
                                     static_cast<__int128>(static_cast<std::int64_t>(b));
            const unsigned __int128 bits = static_cast<unsigned __int128>(product);
            lo_ = static_cast<std::uint64_t>(bits);
            hi_ = static_cast<std::uint64_t>(bits >> 64);
            break;
        }
        case 0x1D: { // DMULTU (unsigned 64-bit multiply)
            const unsigned __int128 product = static_cast<unsigned __int128>(a) *
                                              static_cast<unsigned __int128>(b);
            lo_ = static_cast<std::uint64_t>(product);
            hi_ = static_cast<std::uint64_t>(product >> 64);
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
                const std::uint64_t queryVpn = cp0Registers_[EntryHi] & ~0x1FFFULL;
                const std::uint64_t queryAsid = cp0Registers_[EntryHi] & 0xFFULL;
                for (unsigned int i = 0; i < tlb_.size(); ++i) {
                    const auto &entry = tlb_[i];
                    const std::uint64_t entryVpn = entry.entryHi & ~0x1FFFULL;
                    const bool global = (entry.entryLo0 & 1ULL) && (entry.entryLo1 & 1ULL);
                    if (entryVpn == queryVpn &&
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
        std::uint32_t value = 0;
        if ((address & 3) || !memory.read32(address, value)) {
            raiseException(4, address, true);
            return true;
        }
        registers_[rt] = signExtend32(value);
        llAddress_ = address;
        llBit_ = true;
        cp0Registers_[LLAddr] = address & ~0xFULL;
        return true;
    }
    case 0x20: { // LB
        const std::uint64_t address = a + offset;
        std::uint8_t value = 0;
        if (!memory.read8(address, value)) {
            raiseException(4, address, true);
            return true;
        }
        registers_[rt] = static_cast<std::int64_t>(static_cast<std::int8_t>(value));
        return true;
    }
    case 0x21: { // LH
        const std::uint64_t address = a + offset;
        std::uint16_t value = 0;
        if ((address & 1) || !memory.read16(address, value)) {
            raiseException(4, address, true);
            return true;
        }
        registers_[rt] = static_cast<std::int64_t>(static_cast<std::int16_t>(value));
        return true;
    }
    case 0x22: { // LWL (big-endian)
        const std::uint64_t address = a + offset;
        const std::uint64_t aligned = address & ~3ULL;
        std::uint32_t memoryWord = 0;
        if (!memory.read32(aligned, memoryWord)) {
            raiseException(4, address, true);
            return true;
        }
        const unsigned int byteOffset = static_cast<unsigned int>(address & 3);
        const unsigned int shift = byteOffset * 8;
        const std::uint32_t preserveMask = byteOffset == 0
            ? 0U : (0xFFFFFFFFU >> ((4 - byteOffset) * 8));
        const std::uint32_t merged = (memoryWord << shift) |
                                     (low32(b) & preserveMask);
        registers_[rt] = signExtend32(merged);
        return true;
    }
    case 0x23: { // LW
        const std::uint64_t address = a + offset;
        std::uint32_t value = 0;
        if ((address & 3) || !memory.read32(address, value)) {
            raiseException(4, address, true);
            return true;
        }
        registers_[rt] = signExtend32(value);
        return true;
    }
    case 0x24: { // LBU
        const std::uint64_t address = a + offset;
        std::uint8_t value = 0;
        if (!memory.read8(address, value)) {
            raiseException(4, address, true);
            return true;
        }
        registers_[rt] = value;
        return true;
    }
    case 0x25: { // LHU
        const std::uint64_t address = a + offset;
        std::uint16_t value = 0;
        if ((address & 1) || !memory.read16(address, value)) {
            raiseException(4, address, true);
            return true;
        }
        registers_[rt] = value;
        return true;
    }
    case 0x26: { // LWR (big-endian)
        const std::uint64_t address = a + offset;
        const std::uint64_t aligned = address & ~3ULL;
        std::uint32_t memoryWord = 0;
        if (!memory.read32(aligned, memoryWord)) {
            raiseException(4, address, true);
            return true;
        }
        const unsigned int byteOffset = static_cast<unsigned int>(address & 3);
        const unsigned int shift = (3 - byteOffset) * 8;
        const std::uint32_t preserveMask = byteOffset == 3
            ? 0U : (0xFFFFFFFFU << ((byteOffset + 1) * 8));
        const std::uint32_t merged = (memoryWord >> shift) |
                                     (low32(b) & preserveMask);
        registers_[rt] = signExtend32(merged);
        return true;
    }
    case 0x27: { // LWU
        const std::uint64_t address = a + offset;
        std::uint32_t value = 0;
        if ((address & 3) || !memory.read32(address, value)) {
            raiseException(4, address, true);
            return true;
        }
        registers_[rt] = value;
        return true;
    }
    case 0x37: { // LD
        const std::uint64_t address = a + offset;
        std::uint64_t value = 0;
        if ((address & 7) || !memory.read64(address, value)) {
            raiseException(4, address, true);
            return true;
        }
        registers_[rt] = value;
        return true;
    }
    case 0x28: { // SB
        const std::uint64_t address = a + offset;
        llBit_ = false;
        if (!memory.write8(address, static_cast<std::uint8_t>(b))) {
            raiseException(5, address, true);
            return true;
        }
        return true;
    }
    case 0x29: { // SH
        const std::uint64_t address = a + offset;
        llBit_ = false;
        if ((address & 1) || !memory.write16(address, static_cast<std::uint16_t>(b))) {
            raiseException(5, address, true);
            return true;
        }
        return true;
    }
    case 0x2A: { // SWL (big-endian)
        const std::uint64_t address = a + offset;
        llBit_ = false;
        const std::uint64_t aligned = address & ~3ULL;
        std::uint32_t memoryWord = 0;
        if (!memory.read32(aligned, memoryWord)) {
            raiseException(5, address, true);
            return true;
        }
        const unsigned int byteOffset = static_cast<unsigned int>(address & 3);
        const unsigned int shift = byteOffset * 8;
        const std::uint32_t mask = 0xFFFFFFFFU >> shift;
        const std::uint32_t value = low32(b) >> shift;
        if (!memory.write32(aligned, (memoryWord & ~mask) | (value & mask))) {
            raiseException(5, address, true);
        }
        return true;
    }
    case 0x2E: { // SWR (big-endian)
        const std::uint64_t address = a + offset;
        llBit_ = false;
        const std::uint64_t aligned = address & ~3ULL;
        std::uint32_t memoryWord = 0;
        if (!memory.read32(aligned, memoryWord)) {
            raiseException(5, address, true);
            return true;
        }
        const unsigned int byteOffset = static_cast<unsigned int>(address & 3);
        const unsigned int shift = (3 - byteOffset) * 8;
        const std::uint32_t mask = 0xFFFFFFFFU << shift;
        const std::uint32_t value = low32(b) << shift;
        if (!memory.write32(aligned, (memoryWord & ~mask) | (value & mask))) {
            raiseException(5, address, true);
        }
        return true;
    }
    case 0x2B: { // SW
        const std::uint64_t address = a + offset;
        llBit_ = false;
        if ((address & 3) || !memory.write32(address, low32(b))) {
            raiseException(5, address, true);
            return true;
        }
        return true;
    }
    case 0x38: { // SC (Store Conditional, 32-bit)
        const std::uint64_t address = a + offset;
        if (address & 3) {
            llBit_ = false;
            raiseException(5, address, true);
            return true;
        }
        const bool reservationMatches = llBit_ && llAddress_ == address;
        llBit_ = false;
        if (!reservationMatches) {
            registers_[rt] = 0;
            return true;
        }
        if (!memory.write32(address, low32(b))) {
            raiseException(5, address, true);
            return true;
        }
        registers_[rt] = 1;
        return true;
    }
    case 0x3F: { // SD
        const std::uint64_t address = a + offset;
        llBit_ = false;
        if ((address & 7) || !memory.write64(address, b)) {
            raiseException(5, address, true);
            return true;
        }
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
