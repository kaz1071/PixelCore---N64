#include "core/rsp/rsp.h"

#include "core/cpu/r4300i.h"


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
    for (auto &vector : vectorRegisters_)
        vector.fill(0);
    vectorCarry_ = 0;
    vectorCompare_ = 0;
    vectorClip_ = 0;
    pc_ = 0;
    hi_ = 0;
    lo_ = 0;
    nextPc_ = 4;
    delaySlotPending_ = false;
    halted_ = false;
    broke_ = false;
}

std::uint32_t Rsp::pc() const { return pc_; }
std::uint32_t Rsp::reg(unsigned int index) const
{
    return index < registers_.size() ? registers_[index] : 0;
}
void Rsp::resume()
{
    halted_ = false;
    broke_ = false;
}

void Rsp::setPc(std::uint32_t address)
{
    pc_ = address & 0xFFCU;
    nextPc_ = (pc_ + 4) & 0xFFCU;
    delaySlotPending_ = false;
}

void Rsp::setReg(unsigned int index, std::uint32_t value)
{
    if (index > 0 && index < registers_.size())
        registers_[index] = value;
}

std::uint16_t Rsp::vectorLane(unsigned int index, unsigned int lane) const
{
    if (index >= vectorRegisters_.size() || lane >= 8)
        return 0;
    return vectorRegisters_[index][lane];
}

void Rsp::setVectorLane(unsigned int index, unsigned int lane, std::uint16_t value)
{
    if (index < vectorRegisters_.size() && lane < 8)
        vectorRegisters_[index][lane] = value;
}
bool Rsp::halted() const { return halted_; }
bool Rsp::broke() const { return broke_; }

std::uint32_t Rsp::signExtend16(std::uint32_t value)
{
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(
        static_cast<std::int16_t>(value & 0xFFFFU)));
}

bool Rsp::step(N64Memory &memory, std::string &error)
{
    if (halted_)
        return false;

    error.clear();
    std::uint32_t instruction = 0;
    if (!memory.read32(InstructionMemoryBase + (pc_ & 0xFFCU), instruction))
    {
        error = "RSP instruction fetch failed at IMEM offset " + std::to_string(pc_);
        halted_ = true;
        return false;
    }

    delaySlotPending_ = false;
    pc_ = nextPc_ & 0xFFCU;
    nextPc_ = (pc_ + 4) & 0xFFCU;

    const bool result = execute(instruction, memory, error);
    registers_[0] = 0;
    if (!result)
        halted_ = true;

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
    case 0x12: // COP2 vector arithmetic
    {
        if ((rs & 0x10U) == 0)
        {
            error = "Unsupported RSP COP2 transfer operation " + std::to_string(rs);
            return false;
        }

        const unsigned element = (instruction >> 21) & 0xF;
        const unsigned vt = rt;
        const unsigned vs = rd;
        const unsigned vd = (instruction >> 6) & 31;
        const unsigned function = fn;
        static constexpr unsigned elementMap[16][8] = {
            {0,1,2,3,4,5,6,7}, {0,1,2,3,4,5,6,7},
            {0,0,2,2,4,4,6,6}, {1,1,3,3,5,5,7,7},
            {0,0,0,0,4,4,4,4}, {1,1,1,1,5,5,5,5},
            {2,2,2,2,6,6,6,6}, {3,3,3,3,7,7,7,7},
            {0,0,0,0,0,0,0,0}, {1,1,1,1,1,1,1,1},
            {2,2,2,2,2,2,2,2}, {3,3,3,3,3,3,3,3},
            {4,4,4,4,4,4,4,4}, {5,5,5,5,5,5,5,5},
            {6,6,6,6,6,6,6,6}, {7,7,7,7,7,7,7,7}
        };
        std::array<std::uint16_t, 8> source{};
        std::array<std::uint16_t, 8> left{};
        for (unsigned lane = 0; lane < 8; ++lane)
        {
            left[lane] = vectorRegisters_[vs][lane];
            source[lane] = vectorRegisters_[vt][elementMap[element][lane]];
        }

        switch (function)
        {
        case 0x13: // VABS: absolute value of vt, with sign selected by vs
            for (unsigned lane = 0; lane < 8; ++lane)
            {
                const std::int32_t sign = static_cast<std::int16_t>(left[lane]);
                const std::int32_t value = static_cast<std::int16_t>(source[lane]);
                const std::int32_t result = sign < 0 ? -value
                    : (sign > 0 ? value : 0);
                const std::int32_t saturated = result > 32767 ? 32767
                    : (result < -32768 ? -32768 : result);
                vectorRegisters_[vd][lane] = static_cast<std::uint16_t>(saturated);
            }
            break;
        case 0x14: // VADDC: unsigned add, recording per-lane carry-out in VCO
        {
            std::uint16_t carries = 0;
            for (unsigned lane = 0; lane < 8; ++lane)
            {
                const std::uint32_t sum =
                    static_cast<std::uint32_t>(left[lane]) + source[lane];
                vectorRegisters_[vd][lane] = static_cast<std::uint16_t>(sum);
                if (sum > 0xFFFFU)
                    carries |= static_cast<std::uint16_t>(1U << lane);
            }
            vectorCarry_ = carries;
            vectorCompare_ = 0;
            break;
        }
        case 0x15: // VSUBC: unsigned subtract, recording borrow and nonzero flags
        {
            std::uint16_t flags = 0;
            for (unsigned lane = 0; lane < 8; ++lane)
            {
                const std::uint16_t difference =
                    static_cast<std::uint16_t>(left[lane] - source[lane]);
                vectorRegisters_[vd][lane] = difference;
                if (left[lane] < source[lane])
                    flags |= static_cast<std::uint16_t>(1U << lane);
                if (difference != 0)
                    flags |= static_cast<std::uint16_t>(1U << (lane + 8));
            }
            vectorCarry_ = flags;
            vectorCompare_ = 0;
            break;
        }
        case 0x10: // VADD: signed saturated add with VCO carry-in
        case 0x11: // VSUB: signed saturated subtract with VCO borrow-in
            for (unsigned lane = 0; lane < 8; ++lane)
            {
                const std::int32_t a16 = static_cast<std::int16_t>(left[lane]);
                const std::int32_t b16 = static_cast<std::int16_t>(source[lane]);
                const std::int32_t carry = (vectorCarry_ >> lane) & 1;
                const std::int32_t result = function == 0x10
                    ? a16 + b16 + carry : a16 - b16 - carry;
                const std::int32_t saturated = result > 32767 ? 32767
                    : (result < -32768 ? -32768 : result);
                vectorRegisters_[vd][lane] = static_cast<std::uint16_t>(saturated);
            }
            vectorCarry_ = 0;
            vectorCompare_ = 0;
            break;
        case 0x28: // VAND
        case 0x2A: // VOR
        case 0x2C: // VXOR
            for (unsigned lane = 0; lane < 8; ++lane)
            {
                if (function == 0x28)
                    vectorRegisters_[vd][lane] = left[lane] & source[lane];
                else if (function == 0x2A)
                    vectorRegisters_[vd][lane] = left[lane] | source[lane];
                else
                    vectorRegisters_[vd][lane] = left[lane] ^ source[lane];
            }
            break;
        default:
            error = "Unsupported RSP vector function " + std::to_string(function);
            return false;
        }
        break;
    }
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
        case 0x09: write(rd, nextPc_); branch(true, a); break;
        case 0x0D: halted_ = true; broke_ = true; return true; // BREAK
        case 0x10: write(rd, hi_); break; // MFHI
        case 0x11: hi_ = a; break; // MTHI
        case 0x12: write(rd, lo_); break; // MFLO
        case 0x13: lo_ = a; break; // MTLO
        case 0x18: { // MULT
            const std::int64_t product =
                static_cast<std::int64_t>(signed32(a)) * signed32(b);
            hi_ = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
            lo_ = static_cast<std::uint32_t>(product);
            break;
        }
        case 0x19: { // MULTU
            const std::uint64_t product = static_cast<std::uint64_t>(a) * b;
            hi_ = static_cast<std::uint32_t>(product >> 32);
            lo_ = static_cast<std::uint32_t>(product);
            break;
        }
        case 0x1A: // DIV
            if (b != 0) {
                if (a == 0x80000000U && b == 0xFFFFFFFFU) {
                    lo_ = 0x80000000U;
                    hi_ = 0;
                } else {
                    lo_ = static_cast<std::uint32_t>(signed32(a) / signed32(b));
                    hi_ = static_cast<std::uint32_t>(signed32(a) % signed32(b));
                }
            }
            break;
        case 0x1B: // DIVU
            if (b != 0) {
                lo_ = a / b;
                hi_ = a % b;
            }
            break;
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
    case 0x01: // REGIMM
        if (rt == 0 || rt == 1 || rt == 16 || rt == 17) {
            const bool take = (rt == 0 || rt == 16)
                ? signed32(a) < 0 : signed32(a) >= 0;
            if (take && (rt == 16 || rt == 17))
                write(31, nextPc_);
            branch(take, pc_ + (static_cast<std::uint32_t>(simm) << 2));
        } else {
            error = "Unsupported RSP REGIMM operation " + std::to_string(rt);
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
    case 0x04: branch(a == b, pc_ + (static_cast<std::uint32_t>(simm) << 2)); break;
    case 0x05: branch(a != b, pc_ + (static_cast<std::uint32_t>(simm) << 2)); break;
    case 0x06: branch(signed32(a) <= 0, pc_ + (static_cast<std::uint32_t>(simm) << 2)); break;
    case 0x07: branch(signed32(a) > 0, pc_ + (static_cast<std::uint32_t>(simm) << 2)); break;
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
