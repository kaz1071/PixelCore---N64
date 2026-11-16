#include "core/cpu/r4300i.h"

#include <cstdint>
#include <limits>
#include <sstream>

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
}

void R4300i::raiseException(unsigned int exceptionCode, std::uint64_t faultAddress,
                            bool hasBadAddress)
{
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
        case 0x21: registers_[rd] = signExtend32(low32(a + b)); break; // ADDU
        case 0x23: registers_[rd] = signExtend32(low32(a - b)); break; // SUBU
        case 0x24: registers_[rd] = a & b; break; // AND
        case 0x25: registers_[rd] = a | b; break; // OR
        case 0x26: registers_[rd] = a ^ b; break; // XOR
        case 0x27: registers_[rd] = ~(a | b); break; // NOR
        case 0x2A: registers_[rd] = static_cast<std::int64_t>(a) < static_cast<std::int64_t>(b); break; // SLT
        case 0x2B: registers_[rd] = a < b; break; // SLTU
        case 0x2C: registers_[rd] = a + b; break; // DADD (overflow trap not yet modeled)
        case 0x2D: registers_[rd] = a + b; break; // DADDU
        case 0x2E: registers_[rd] = a - b; break; // DSUB (overflow trap not yet modeled)
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
    case 0x08: registers_[rt] = signExtend32(low32(a + offset)); return true; // ADDI (overflow trap not yet modeled)
    case 0x09: registers_[rt] = signExtend32(low32(a + offset)); return true; // ADDIU
    case 0x0A: registers_[rt] = static_cast<std::int64_t>(a) < offset; return true; // SLTI
    case 0x0B: registers_[rt] = a < static_cast<std::uint64_t>(offset); return true; // SLTIU
    case 0x0C: registers_[rt] = a & immediate; return true; // ANDI
    case 0x0D: registers_[rt] = a | immediate; return true; // ORI
    case 0x0E: registers_[rt] = a ^ immediate; return true; // XORI
    case 0x0F: registers_[rt] = signExtend32(static_cast<std::uint32_t>(immediate) << 16); return true; // LUI
    case 0x18: registers_[rt] = a + offset; return true; // DADDI (overflow trap not yet modeled)
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
        raiseException(11, instructionAddress); // Coprocessor Unusable / unsupported COP0 operation.
        return true;
    case 0x1C: // SPECIAL2 is not part of the base R4300i ISA.
        break;
    case 0x23: { // LW
        const std::uint64_t address = a + offset;
        std::uint32_t value = 0;
        if ((address & 3) != 0 || !memory.read32(address, value)) {
            raiseException(4, address, true); // Address Error on load.
            return true;
        }
        registers_[rt] = signExtend32(value);
        return true;
    }
    case 0x2B: { // SW
        const std::uint64_t address = a + offset;
        if ((address & 3) != 0 || !memory.write32(address, low32(b))) {
            raiseException(5, address, true); // Address Error on store.
            return true;
        }
        return true;
    }
    default: break;
    }

    std::ostringstream message;
    message << "Unsupported R4300i opcode 0x" << std::hex << opcode
            << " at 0x" << instructionAddress;
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
