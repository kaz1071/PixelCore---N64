#include "core/rsp/rsp.h"

#include "core/cpu/r4300i.h"

#include <sstream>

namespace
{
std::int32_t signed32(std::uint32_t value)
{
    return static_cast<std::int32_t>(value);
}
}

void Rsp::reset()
{
    registers_.fill(0);
    pc_ = 0;
    nextPc_ = 4;
    delaySlotPending_ = false;
    currentInstructionInDelaySlot_ = false;
    halted_ = false;
}

std::uint32_t Rsp::pc() const { return pc_; }
std::uint32_t Rsp::reg(unsigned int index) const
{
    return index < registers_.size() ? registers_[index] : 0;
}
void Rsp::setReg(unsigned int index, std::uint32_t value)
{
    if (index > 0 && index < registers_.size())
        registers_[index] = value;
}
bool Rsp::halted() const { return halted_; }

std::uint32_t Rsp::signExtend16(std::uint32_t value)
{
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(
        static_cast<std::int16_t>(value & 0xFFFFU)));
}

bool Rsp::step(N64Memory &memory, std::string &error)
{
    if (halted_)
        return false;

    std::uint32_t instruction = 0;
    if (!memory.read32(InstructionMemoryBase + (pc_ & 0xFFCU), instruction))
    {
        error = "RSP instruction fetch failed at IMEM offset " + std::to_string(pc_);
        halted_ = true;
        return false;
    }

    currentInstructionInDelaySlot_ = delaySlotPending_;
    delaySlotPending_ = false;
    pc_ = nextPc_ & 0xFFCU;
    nextPc_ = (pc_ + 4) & 0xFFCU;

    const bool result = execute(instruction, memory, error);
    registers_[0] = 0;
    if (!result)
        halted_ = true;

    currentInstructionInDelaySlot_ = false;
    return result;
}

bool Rsp::execute(std::uint32_t instruction, N64Memory &memory,
                  std::string &error)
{
    const unsigned op = instruction >> 26;
    const unsigned rs = (instruction >> 21) & 31;
    const unsigned rt = (instruction >> 16) & 31;
    const unsigned rd = (instruction >> 11) & 31;
    const unsigned sa = (instruction >> 6) & 31;
    const unsigned fn = instruction & 63;
    const std::uint32_t imm = instruction & 0xFFFFU;
    const std::int32_t simm = static_cast<std::int16_t>(imm);
    const std::uint32_t a = registers_[rs];
    const std::uint32_t b = registers_[rt];
    const std::uint32_t effective = (a + static_cast<std::uint32_t>(simm)) & 0xFFFU;
    auto write = [this](unsigned index, std::uint32_t value) {
        if (index != 0) registers_[index] = value;
    };
    auto branch = [this](bool take, std::uint32_t target) {
        if (take) {
            nextPc_ = target & 0xFFCU;
            delaySlotPending_ = true;
        }
    };
    auto load8 = [&](std::uint32_t address, std::uint8_t &value) {
        return memory.read8(DataMemoryBase + (address & 0xFFFU), value);
    };
    auto load16 = [&](std::uint32_t address, std::uint16_t &value) {
        return memory.read16(DataMemoryBase + (address & 0xFFEU), value);
    };
    auto load32 = [&](std::uint32_t address, std::uint32_t &value) {
        return memory.read32(DataMemoryBase + (address & 0xFFCU), value);
    };
    auto store8 = [&](std::uint32_t address, std::uint8_t value) {
        return memory.write8(DataMemoryBase + (address & 0xFFFU), value);
    };
    auto store16 = [&](std::uint32_t address, std::uint16_t value) {
        return memory.write16(DataMemoryBase + (address & 0xFFEU), value);
    };
    auto store32 = [&](std::uint32_t address, std::uint32_t value) {
        return memory.write32(DataMemoryBase + (address & 0xFFCU), value);
    };
    auto memoryFailure = [&]() {
        error = "RSP data-memory access failed at offset " + std::to_string(effective);
        return false;
    };

    switch (op)
    {
    case 0x00:
        switch (fn)
        {
        case 0x00: write(rd, b << sa); break; // SLL
        case 0x02: write(rd, b >> sa); break; // SRL
        case 0x03: write(rd, static_cast<std::uint32_t>(signed32(b) >> sa)); break;
        case 0x04: write(rd, b << (a & 31)); break;
        case 0x06: write(rd, b >> (a & 31)); break;
        case 0x07: write(rd, static_cast<std::uint32_t>(signed32(b) >> (a & 31))); break;
        case 0x08: branch(true, a); break; // JR
        case 0x09: write(rd == 0 ? 31 : rd, nextPc_); branch(true, a); break;
        case 0x0D: halted_ = true; return true; // BREAK
        case 0x21: write(rd, a + b); break; // ADDU
        case 0x23: write(rd, a - b); break; // SUBU
        case 0x24: write(rd, a & b); break;
        case 0x25: write(rd, a | b); break;
        case 0x26: write(rd, a ^ b); break;
        case 0x27: write(rd, ~(a | b)); break;
        case 0x2A: write(rd, signed32(a) < signed32(b)); break;
        case 0x2B: write(rd, a < b); break;
        default:
            error = "Unsupported RSP SPECIAL function " + std::to_string(fn);
            return false;
        }
        break;
    case 0x02: // J
    case 0x03: // JAL
    {
        const std::uint32_t target = (instruction & 0x03FFFFFFU) << 2;
        if (op == 0x03) write(31, nextPc_);
        branch(true, target);
        break;
    }
    case 0x04: branch(a == b, nextPc_ + (static_cast<std::uint32_t>(simm) << 2)); break;
    case 0x05: branch(a != b, nextPc_ + (static_cast<std::uint32_t>(simm) << 2)); break;
    case 0x06: branch(signed32(a) <= 0, nextPc_ + (static_cast<std::uint32_t>(simm) << 2)); break;
    case 0x07: branch(signed32(a) > 0, nextPc_ + (static_cast<std::uint32_t>(simm) << 2)); break;
    case 0x08: write(rt, a + static_cast<std::uint32_t>(simm)); break; // ADDI (wrap)
    case 0x09: write(rt, a + static_cast<std::uint32_t>(simm)); break;
    case 0x0A: write(rt, signed32(a) < simm); break;
    case 0x0B: write(rt, a < signExtend16(imm)); break;
    case 0x0C: write(rt, a & imm); break;
    case 0x0D: write(rt, a | imm); break;
    case 0x0E: write(rt, a ^ imm); break;
    case 0x0F: write(rt, imm << 16); break;
    case 0x20: { std::uint8_t v=0; if(!load8(effective,v)) return memoryFailure(); write(rt, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(v)))); break; }
    case 0x24: { std::uint8_t v=0; if(!load8(effective,v)) return memoryFailure(); write(rt, v); break; }
    case 0x21: { std::uint16_t v=0; if(!load16(effective,v)) return memoryFailure(); write(rt, signExtend16(v)); break; }
    case 0x25: { std::uint16_t v=0; if(!load16(effective,v)) return memoryFailure(); write(rt, v); break; }
    case 0x23: { std::uint32_t v=0; if(!load32(effective,v)) return memoryFailure(); write(rt, v); break; }
    case 0x28: if(!store8(effective,static_cast<std::uint8_t>(b))) return memoryFailure(); break;
    case 0x29: if(!store16(effective,static_cast<std::uint16_t>(b))) return memoryFailure(); break;
    case 0x2B: if(!store32(effective,b)) return memoryFailure(); break;
    default:
        error = "Unsupported RSP opcode " + std::to_string(op);
        return false;
    }
    return true;
}
