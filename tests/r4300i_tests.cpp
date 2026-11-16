#include "core/cpu/r4300i.h"

#include <cstdint>
#include <iostream>
#include <unordered_map>

class TestMemory final : public N64Memory
{
public:
    std::unordered_map<std::uint64_t, std::uint32_t> words;

    bool read32(std::uint64_t address, std::uint32_t &value) override
    {
        const auto it = words.find(address);
        if (it == words.end()) return false;
        value = it->second;
        return true;
    }

    bool write32(std::uint64_t address, std::uint32_t value) override
    {
        words[address] = value;
        return true;
    }
};

static std::uint32_t iType(unsigned int op, unsigned int rs, unsigned int rt,
                           std::uint16_t imm)
{
    return (op << 26) | (rs << 21) | (rt << 16) | imm;
}

static std::uint32_t rType(unsigned int rs, unsigned int rt, unsigned int rd,
                           unsigned int shamt, unsigned int fn)
{
    return (rs << 21) | (rt << 16) | (rd << 11) | (shamt << 6) | fn;
}

static bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

int main()
{
    bool passed = true;
    std::string error;
    R4300i cpu;
    TestMemory memory;
    const auto base = R4300i::ResetAddress;

    // ADDIU + ADDU: 5 + 7 = 12.
    memory.words[base] = iType(0x09, 0, 1, 5);
    memory.words[base + 4] = iType(0x09, 0, 2, 7);
    memory.words[base + 8] = rType(1, 2, 3, 0, 0x21);
    cpu.reset();
    passed &= check(cpu.step(memory, error), "ADDIU instruction 1 executes");
    passed &= check(cpu.step(memory, error), "ADDIU instruction 2 executes");
    passed &= check(cpu.step(memory, error), "ADDU instruction executes");
    passed &= check(cpu.reg(3) == 12, "ADDU produces 12");

    // A taken BEQ must execute its delay slot and skip the following word.
    memory.words.clear();
    memory.words[base] = iType(0x04, 0, 0, 2); // BEQ r0,r0,+2
    memory.words[base + 4] = iType(0x09, 0, 1, 9); // delay slot
    memory.words[base + 8] = iType(0x09, 0, 2, 1); // skipped
    memory.words[base + 12] = iType(0x09, 0, 3, 4); // branch target
    cpu.reset();
    passed &= check(cpu.step(memory, error), "BEQ executes");
    passed &= check(cpu.step(memory, error), "branch delay slot executes");
    passed &= check(cpu.reg(1) == 9, "delay slot instruction took effect");
    passed &= check(cpu.pc() == base + 12, "branch reaches target after delay slot");
    passed &= check(cpu.step(memory, error), "branch target executes");
    passed &= check(cpu.reg(2) == 0 && cpu.reg(3) == 4, "branch skips untaken path");

    // MTC0 / MFC0 can write and read a CP0 register.
    memory.words.clear();
    memory.words[base] = iType(0x09, 0, 1, 0x1234);
    memory.words[base + 4] = (0x10U << 26) | (0x04U << 21) | (1U << 16) | (14U << 11);
    memory.words[base + 8] = (0x10U << 26) | (1U << 16) | (14U << 11);
    cpu.reset();
    passed &= check(cpu.step(memory, error), "prepare CP0 value");
    passed &= check(cpu.step(memory, error), "MTC0 executes");
    passed &= check(cpu.step(memory, error), "MFC0 executes");
    passed &= check(cpu.reg(1) == 0x1234, "MFC0 reads value written by MTC0");

    // An unsupported instruction raises Reserved Instruction and selects BEV vector.
    memory.words.clear();
    memory.words[base] = 0xFFFFFFFFU;
    cpu.reset();
    passed &= check(cpu.step(memory, error), "reserved instruction is handled as exception");
    passed &= check(cpu.pc() == R4300i::BootstrapExceptionVector,
                    "BEV selects bootstrap exception vector");
    passed &= check(cpu.cp0(R4300i::EPC) == base, "exception records EPC");
    passed &= check(((cpu.cp0(R4300i::Cause) >> 2) & 0x1F) == 10,
                    "exception records Reserved Instruction code");

    if (!passed) return 1;
    std::cout << "All R4300i CPU tests passed.\n";
    return 0;
}
