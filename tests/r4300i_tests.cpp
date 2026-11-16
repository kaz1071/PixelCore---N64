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
    memory.words[base] = 0x70000000U; // Reserved SPECIAL2 opcode
    cpu.reset();
    passed &= check(cpu.step(memory, error), "reserved instruction is handled as exception");
    passed &= check(cpu.pc() == R4300i::BootstrapExceptionVector,
                    "BEV selects bootstrap exception vector");
    passed &= check(cpu.cp0(R4300i::EPC) == base, "exception records EPC");
    passed &= check(((cpu.cp0(R4300i::Cause) >> 2) & 0x1F) == 10,
                    "exception records Reserved Instruction code");


    // Big-endian byte/halfword and 64-bit load/store operations.
    memory.words.clear();
    memory.words[0x100] = 0;
    memory.words[base] = iType(0x09, 0, 1, 0x100); // base address
    memory.words[base + 4] = iType(0x09, 0, 2, 0x80); // sign bit for LB
    memory.words[base + 8] = iType(0x28, 1, 2, 1); // SB at 0x101
    memory.words[base + 12] = iType(0x24, 1, 3, 1); // LBU
    memory.words[base + 16] = iType(0x20, 1, 4, 1); // LB
    memory.words[base + 20] = iType(0x09, 0, 5, 0xFEDC); // value for SH
    memory.words[base + 24] = iType(0x29, 1, 5, 2); // SH at 0x102
    memory.words[base + 28] = iType(0x25, 1, 6, 2); // LHU
    memory.words[base + 32] = iType(0x21, 1, 7, 2); // LH
    cpu.reset();
    for (int i = 0; i < 9; ++i)
        passed &= check(cpu.step(memory, error), "byte/halfword instruction executes");
    passed &= check(cpu.reg(3) == 0x80, "LBU zero extends byte");
    passed &= check(cpu.reg(4) == 0xFFFFFFFFFFFFFF80ULL, "LB sign extends byte");
    passed &= check(cpu.reg(6) == 0xFEDC, "LHU zero extends halfword");
    passed &= check(cpu.reg(7) == 0xFFFFFFFFFFFFFEDCULL, "LH sign extends halfword");

    memory.words.clear();
    memory.words[base] = iType(0x09, 0, 1, 0x200); // base address
    memory.words[base + 4] = iType(0x3F, 1, 2, 0); // SD
    memory.words[base + 8] = iType(0x37, 1, 3, 0); // LD
    cpu.reset();
    cpu.setReg(2, 0x123456789ABCDEF0ULL);
    passed &= check(cpu.step(memory, error), "prepare doubleword address");
    passed &= check(cpu.step(memory, error), "SD executes");
    passed &= check(cpu.step(memory, error), "LD executes");
    passed &= check(cpu.reg(3) == 0x123456789ABCDEF0ULL, "LD retrieves stored 64-bit value");

    // Misaligned LH raises Address Error Load (AdEL).
    memory.words.clear();
    memory.words[base] = iType(0x21, 0, 1, 1);
    cpu.reset();
    passed &= check(cpu.step(memory, error), "misaligned LH is handled");
    passed &= check(((cpu.cp0(R4300i::Cause) >> 2) & 0x1F) == 4,
                    "misaligned LH raises AdEL");
    passed &= check(cpu.cp0(R4300i::BadVAddr) == 1, "AdEL records BadVAddr");


    // LWL/LWR merge the correct bytes for every address offset (big-endian).
    const std::uint32_t loadLeftExpected[4] = {
        0x11223344U, 0x223344DDU, 0x3344CCDDU, 0x44BBCCDDU
    };
    const std::uint32_t loadRightExpected[4] = {
        0xAABBCC11U, 0xAABB1122U, 0xAA112233U, 0x11223344U
    };
    for (unsigned int k = 0; k < 4; ++k) {
        memory.words.clear();
        memory.words[base] = iType(0x22, 1, 2, 0); // LWL
        memory.words[0x100] = 0x11223344U;
        cpu.reset();
        cpu.setReg(1, 0x100 + k);
        cpu.setReg(2, 0xAABBCCDDU);
        passed &= check(cpu.step(memory, error), "LWL executes");
        passed &= check(static_cast<std::uint32_t>(cpu.reg(2)) == loadLeftExpected[k],
                        "LWL merges expected big-endian bytes");

        memory.words.clear();
        memory.words[base] = iType(0x26, 1, 2, 0); // LWR
        memory.words[0x100] = 0x11223344U;
        cpu.reset();
        cpu.setReg(1, 0x100 + k);
        cpu.setReg(2, 0xAABBCCDDU);
        passed &= check(cpu.step(memory, error), "LWR executes");
        passed &= check(static_cast<std::uint32_t>(cpu.reg(2)) == loadRightExpected[k],
                        "LWR merges expected big-endian bytes");
    }

    // SWL/SWR preserve untouched bytes for every address offset.
    const std::uint32_t storeLeftExpected[4] = {
        0x11223344U, 0xAA112233U, 0xAABB1122U, 0xAABBCC11U
    };
    const std::uint32_t storeRightExpected[4] = {
        0x44BBCCDDU, 0x3344CCDDU, 0x223344DDU, 0x11223344U
    };
    for (unsigned int k = 0; k < 4; ++k) {
        memory.words.clear();
        memory.words[base] = iType(0x2A, 1, 2, 0); // SWL
        memory.words[0x100] = 0xAABBCCDDU;
        cpu.reset();
        cpu.setReg(1, 0x100 + k);
        cpu.setReg(2, 0x11223344U);
        passed &= check(cpu.step(memory, error), "SWL executes");
        passed &= check(memory.words[0x100] == storeLeftExpected[k],
                        "SWL stores expected big-endian bytes");

        memory.words.clear();
        memory.words[base] = iType(0x2E, 1, 2, 0); // SWR
        memory.words[0x100] = 0xAABBCCDDU;
        cpu.reset();
        cpu.setReg(1, 0x100 + k);
        cpu.setReg(2, 0x11223344U);
        passed &= check(cpu.step(memory, error), "SWR executes");
        passed &= check(memory.words[0x100] == storeRightExpected[k],
                        "SWR stores expected big-endian bytes");
    }

    if (!passed) return 1;
    std::cout << "All R4300i CPU tests passed.\n";
    return 0;
}
