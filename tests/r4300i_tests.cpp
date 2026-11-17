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
        // The CPU fetches reset code through the KSEG1 physical alias.
        if (address >= 0x1FC00000ULL && address < 0x1FC01000ULL)
            address = R4300i::ResetAddress + (address - 0x1FC00000ULL);
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

    // LL/SC: a matching reservation stores and reports success.
    memory.words.clear();
    memory.words[0x300] = 0x01020304U;
    memory.words[base] = iType(0x30, 1, 2, 0); // LL r2,0(r1)
    memory.words[base + 4] = iType(0x38, 1, 2, 0); // SC r2,0(r1)
    cpu.reset();
    cpu.setReg(1, 0x300);
    cpu.setReg(2, 0xAABBCCDDU);
    passed &= check(cpu.step(memory, error), "LL executes");
    passed &= check(cpu.reg(2) == 0x01020304U, "LL loads and sign-extends the word");
    passed &= check(cpu.step(memory, error), "SC executes with a reservation");
    passed &= check(cpu.reg(2) == 1, "SC reports success in its target register");
    passed &= check(memory.words[0x300] == 0x01020304U, "SC stores the loaded value");

    // SC without a preceding LL must fail without modifying memory.
    memory.words.clear();
    memory.words[0x300] = 0x55667788U;
    memory.words[base] = iType(0x38, 1, 2, 0); // SC without LL
    cpu.reset();
    cpu.setReg(1, 0x300);
    cpu.setReg(2, 0xDEADBEEFU);
    passed &= check(cpu.step(memory, error), "SC without reservation is handled");
    passed &= check(cpu.reg(2) == 0, "SC without reservation reports failure");
    passed &= check(memory.words[0x300] == 0x55667788U,
                    "failed SC leaves memory unchanged");

    // An ordinary store between LL and SC invalidates the reservation.
    memory.words.clear();
    memory.words[0x300] = 0x11111111U;
    memory.words[0x304] = 0;
    memory.words[base] = iType(0x30, 1, 2, 0); // LL
    memory.words[base + 4] = iType(0x2B, 1, 3, 4); // SW to another word
    memory.words[base + 8] = iType(0x38, 1, 2, 0); // SC
    cpu.reset();
    cpu.setReg(1, 0x300);
    cpu.setReg(2, 0xAABBCCDDU);
    cpu.setReg(3, 0x12345678U);
    passed &= check(cpu.step(memory, error), "LL before invalidating store executes");
    passed &= check(cpu.step(memory, error), "ordinary store invalidates LL reservation");
    passed &= check(cpu.step(memory, error), "SC after ordinary store executes");
    passed &= check(cpu.reg(2) == 0, "SC fails after an intervening store");
    passed &= check(memory.words[0x300] == 0x11111111U,
                    "failed SC after intervening store leaves target unchanged");

    // Signed conditional branches and branch-and-link instructions.
    memory.words.clear();
    memory.words[base] = iType(0x09, 0, 1, 0xFFFF); // r1 = -1
    memory.words[base + 4] = iType(0x01, 1, 0x00, 2); // BLTZ r1,+2
    memory.words[base + 8] = iType(0x09, 0, 2, 7); // delay slot
    memory.words[base + 12] = iType(0x09, 0, 3, 99); // skipped
    memory.words[base + 16] = iType(0x09, 0, 4, 11); // target
    cpu.reset();
    passed &= check(cpu.step(memory, error), "prepare negative branch operand");
    passed &= check(cpu.step(memory, error), "BLTZ executes");
    passed &= check(cpu.step(memory, error), "BLTZ delay slot executes");
    passed &= check(cpu.reg(2) == 7 && cpu.pc() == base + 16,
                    "BLTZ takes branch and preserves its delay slot");
    passed &= check(cpu.step(memory, error), "BLTZ target executes");
    passed &= check(cpu.reg(3) == 0 && cpu.reg(4) == 11,
                    "BLTZ skips fall-through instruction");

    memory.words.clear();
    memory.words[base] = iType(0x09, 0, 1, 5); // r1 = +5
    memory.words[base + 4] = iType(0x01, 1, 0x11, 2); // BGEZAL r1,+2
    memory.words[base + 8] = iType(0x09, 0, 2, 13); // delay slot
    memory.words[base + 16] = iType(0x09, 0, 3, 17); // target
    cpu.reset();
    passed &= check(cpu.step(memory, error), "prepare nonnegative branch operand");
    passed &= check(cpu.step(memory, error), "BGEZAL executes");
    passed &= check(cpu.reg(31) == base + 12, "BGEZAL writes return address");
    passed &= check(cpu.step(memory, error), "BGEZAL delay slot executes");
    passed &= check(cpu.pc() == base + 16 && cpu.reg(2) == 13,
                    "BGEZAL branches after its delay slot");
    passed &= check(cpu.step(memory, error), "BGEZAL target executes");
    passed &= check(cpu.reg(3) == 17, "BGEZAL reaches branch target");

    // BLEZ and BGTZ compare signed register values.
    memory.words.clear();
    memory.words[base] = iType(0x09, 0, 1, 0); // r1 = 0
    memory.words[base + 4] = iType(0x06, 1, 0, 2); // BLEZ r1,+2
    memory.words[base + 8] = iType(0x09, 0, 2, 19); // delay slot
    memory.words[base + 16] = iType(0x07, 1, 0, 2); // BGTZ r1,+2 (not taken)
    memory.words[base + 20] = iType(0x09, 0, 3, 23); // delay slot
    cpu.reset();
    passed &= check(cpu.step(memory, error), "prepare zero branch operand");
    passed &= check(cpu.step(memory, error), "BLEZ executes");
    passed &= check(cpu.step(memory, error), "BLEZ delay slot executes");
    passed &= check(cpu.pc() == base + 16 && cpu.reg(2) == 19,
                    "BLEZ takes branch when register is zero");
    passed &= check(cpu.step(memory, error), "BGTZ executes");
    passed &= check(cpu.step(memory, error), "not-taken BGTZ still executes delay slot");
    passed &= check(cpu.pc() == base + 24 && cpu.reg(3) == 23,
                    "BGTZ does not branch when register is zero");


    // Signed arithmetic instructions raise Integer Overflow (Ov, code 12).
    const auto expectOverflow = [&](std::uint32_t instruction,
                                    std::uint64_t lhs, std::uint64_t rhs,
                                    const char *name) {
        memory.words.clear();
        memory.words[base] = instruction;
        cpu.reset();
        cpu.setReg(1, lhs);
        cpu.setReg(2, rhs);
        const bool executed = cpu.step(memory, error);
        return check(executed, name) &&
               check(((cpu.cp0(R4300i::Cause) >> 2) & 0x1F) == 12,
                     "signed arithmetic raises Integer Overflow") &&
               check(cpu.cp0(R4300i::EPC) == base,
                     "overflow exception records faulting instruction") &&
               check(cpu.pc() == R4300i::BootstrapExceptionVector,
                     "overflow exception uses bootstrap vector while BEV is set");
    };

    passed &= expectOverflow(rType(1, 2, 3, 0, 0x20),
                             0x7FFFFFFFULL, 1, "ADD positive overflow is handled");
    passed &= expectOverflow(rType(1, 2, 3, 0, 0x20),
                             0xFFFFFFFF80000000ULL, 0xFFFFFFFFFFFFFFFFULL,
                             "ADD negative overflow is handled");
    passed &= expectOverflow(iType(0x08, 1, 2, 1),
                             0x7FFFFFFFULL, 0, "ADDI positive overflow is handled");
    passed &= expectOverflow(rType(1, 2, 3, 0, 0x22),
                             0xFFFFFFFF80000000ULL, 1,
                             "SUB negative overflow is handled");
    passed &= expectOverflow(rType(1, 2, 3, 0, 0x2C),
                             0x7FFFFFFFFFFFFFFFULL, 1,
                             "DADD positive overflow is handled");
    passed &= expectOverflow(iType(0x18, 1, 2, 1),
                             0x7FFFFFFFFFFFFFFFULL, 0,
                             "DADDI positive overflow is handled");
    passed &= expectOverflow(rType(1, 2, 3, 0, 0x2E),
                             0x8000000000000000ULL, 1,
                             "DSUB negative overflow is handled");

    // Non-overflowing signed arithmetic writes the correctly sign-extended result.
    memory.words.clear();
    memory.words[base] = rType(1, 2, 3, 0, 0x20); // ADD
    cpu.reset();
    cpu.setReg(1, 20);
    cpu.setReg(2, 22);
    passed &= check(cpu.step(memory, error) && cpu.reg(3) == 42,
                    "ADD produces a normal signed result");
    memory.words[base] = rType(1, 2, 3, 0, 0x22); // SUB
    cpu.reset();
    cpu.setReg(1, 20);
    cpu.setReg(2, 22);
    passed &= check(cpu.step(memory, error) &&
                    cpu.reg(3) == 0xFFFFFFFFFFFFFFFEULL,
                    "SUB sign-extends a negative 32-bit result");


    // 64-bit multiply/divide instructions use the full HI/LO datapath.
    memory.words.clear();
    memory.words[base] = rType(1, 2, 0, 0, 0x1C); // DMULT
    cpu.reset();
    cpu.setReg(1, 0xFFFFFFFFFFFFFFFEULL); // -2
    cpu.setReg(2, 3);
    passed &= check(cpu.step(memory, error), "DMULT executes");
    passed &= check(cpu.lo() == 0xFFFFFFFFFFFFFFFAULL &&
                    cpu.hi() == 0xFFFFFFFFFFFFFFFFULL,
                    "DMULT produces signed 128-bit product");

    memory.words[base] = rType(1, 2, 0, 0, 0x1D); // DMULTU
    cpu.reset();
    cpu.setReg(1, 0xFFFFFFFFFFFFFFFFULL);
    cpu.setReg(2, 2);
    passed &= check(cpu.step(memory, error), "DMULTU executes");
    passed &= check(cpu.lo() == 0xFFFFFFFFFFFFFFFEULL && cpu.hi() == 1,
                    "DMULTU produces unsigned 128-bit product");

    memory.words[base] = rType(1, 2, 0, 0, 0x1E); // DDIV
    cpu.reset();
    cpu.setReg(1, static_cast<std::uint64_t>(-100LL));
    cpu.setReg(2, 7);
    passed &= check(cpu.step(memory, error), "DDIV executes");
    passed &= check(cpu.lo() == static_cast<std::uint64_t>(-14LL) &&
                    cpu.hi() == static_cast<std::uint64_t>(-2LL),
                    "DDIV stores signed quotient and remainder");

    memory.words[base] = rType(1, 2, 0, 0, 0x1F); // DDIVU
    cpu.reset();
    cpu.setReg(1, 100);
    cpu.setReg(2, 7);
    passed &= check(cpu.step(memory, error), "DDIVU executes");
    passed &= check(cpu.lo() == 14 && cpu.hi() == 2,
                    "DDIVU stores unsigned quotient and remainder");

    // Divide by zero leaves HI/LO unchanged.
    cpu.reset();
    cpu.setReg(1, 123);
    cpu.setReg(2, 0);
    passed &= check(cpu.step(memory, error), "DDIVU by zero is handled");
    passed &= check(cpu.lo() == 0 && cpu.hi() == 0,
                    "DDIVU by zero preserves HI/LO");


    // TLB management instructions: indexed write/read and probe by VPN2 + ASID.
    const std::uint32_t cop0 = 0x10U << 26;
    const std::uint32_t tlbwi = cop0 | (0x10U << 21) | 0x02U;
    const std::uint32_t tlbr = cop0 | (0x10U << 21) | 0x01U;
    const std::uint32_t tlbp = cop0 | (0x10U << 21) | 0x08U;
    cpu.reset();
    cpu.setCp0(R4300i::Index, 5);
    cpu.setCp0(R4300i::PageMask, 0);
    cpu.setCp0(R4300i::EntryHi, 0x0045602A);
    cpu.setCp0(R4300i::EntryLo0, 0x12344);
    cpu.setCp0(R4300i::EntryLo1, 0x23456);
    memory.words.clear();
    memory.words[base] = tlbwi;
    passed &= check(cpu.step(memory, error), "TLBWI executes");
    cpu.setCp0(R4300i::EntryHi, 0);
    memory.words[base + 4] = tlbr;
    passed &= check(cpu.step(memory, error), "TLBR executes");
    passed &= check(cpu.cp0(R4300i::EntryHi) == 0x0045602A &&
                    cpu.cp0(R4300i::EntryLo0) == 0x12344 &&
                    cpu.cp0(R4300i::EntryLo1) == 0x23456,
                    "TLBR restores indexed TLB entry into CP0 registers");

    memory.words[base + 8] = tlbp;
    memory.words[base + 12] = tlbp;
    cpu.setCp0(R4300i::EntryHi, 0x0045602A);
    passed &= check(cpu.step(memory, error), "TLBP executes");
    passed &= check(cpu.cp0(R4300i::Index) == 5,
                    "TLBP finds matching VPN2 and ASID");

    cpu.setCp0(R4300i::EntryHi, 0x0045602B); // Different ASID, not global.
    passed &= check(cpu.step(memory, error), "TLBP handles nonmatching ASID");
    passed &= check((cpu.cp0(R4300i::Index) & 0x80000000ULL) != 0,
                    "TLBP sets probe-failure bit when ASID differs");

    // Global bit in both EntryLo halves permits a match across ASIDs.
    cpu.setCp0(R4300i::EntryHi, 0x0045602A);
    cpu.setCp0(R4300i::EntryLo0, 0x12345 | 1);
    cpu.setCp0(R4300i::EntryLo1, 0x23457 | 1);
    cpu.setCp0(R4300i::Index, 6);
    memory.words[base + 16] = tlbwi;
    passed &= check(cpu.step(memory, error), "write global TLB entry");
    cpu.setCp0(R4300i::EntryHi, 0x0045607F);
    memory.words[base + 20] = tlbp;
    passed &= check(cpu.step(memory, error), "probe global TLB entry");
    passed &= check(cpu.cp0(R4300i::Index) == 6,
                    "TLBP matches global entry regardless of ASID");

    // PageMask allows a TLB entry to cover a larger virtual page pair.
    cpu.setCp0(R4300i::EntryHi, 0x0080002A);
    cpu.setCp0(R4300i::PageMask, 0x00006000); // 16 KiB pages
    cpu.setCp0(R4300i::EntryLo0, 0x34564);
    cpu.setCp0(R4300i::EntryLo1, 0x45676);
    cpu.setCp0(R4300i::Index, 7);
    memory.words[base + 24] = tlbwi;
    passed &= check(cpu.step(memory, error), "write large-page TLB entry");
    cpu.setCp0(R4300i::EntryHi, 0x0080602A); // Same 16 KiB page pair
    memory.words[base + 28] = tlbp;
    passed &= check(cpu.step(memory, error), "probe large-page TLB entry");
    passed &= check(cpu.cp0(R4300i::Index) == 7,
                    "TLBP honors PageMask when matching larger pages");

    // Virtual-address translation: KSEG0 maps directly and a TLB entry
    // translates a mapped virtual page for LW/SW.
    memory.words.clear();
    memory.words[base] = iType(0x23, 1, 2, 0); // LW r2,0(r1)
    memory.words[base + 4] = iType(0x2B, 1, 3, 4); // SW r3,4(r1)
    memory.words[0x100] = 0xCAFEBABEU;
    cpu.reset();
    cpu.setCp0(R4300i::Status, 0x00400000); // Kernel mode, ERL clear.
    cpu.setReg(1, 0xFFFFFFFF80000100ULL); // KSEG0 -> physical 0x100.
    cpu.setReg(3, 0x12345678);
    passed &= check(cpu.step(memory, error), "KSEG0 virtual LW executes");
    passed &= check(cpu.reg(2) == 0xFFFFFFFFCAFEBABEULL,
                    "KSEG0 virtual address maps to physical memory");
    passed &= check(cpu.step(memory, error), "KSEG0 virtual SW executes");
    passed &= check(memory.words[0x104] == 0x12345678,
                    "KSEG0 virtual store writes translated physical address");

    // Install a 4 KiB TLB page: VPN2 0x00400000, ASID 0x2A,
    // even page PFN 0x20 (physical 0x20000), valid and dirty.
    memory.words.clear();
    memory.words[base] = tlbwi;
    memory.words[base + 4] = iType(0x23, 1, 2, 0);
    memory.words[0x20080] = 0x0BADF00DU;
    cpu.reset();
    cpu.setCp0(R4300i::Status, 0x00400000); // Kernel mode, ERL clear.
    cpu.setCp0(R4300i::Index, 0);
    cpu.setCp0(R4300i::EntryHi, 0x0040002A);
    cpu.setCp0(R4300i::PageMask, 0);
    cpu.setCp0(R4300i::EntryLo0, (0x20ULL << 6) | 0x7ULL);
    cpu.setCp0(R4300i::EntryLo1, (0x21ULL << 6) | 0x7ULL);
    passed &= check(cpu.step(memory, error), "install TLB translation entry");
    cpu.setReg(1, 0x00400080);
    passed &= check(cpu.step(memory, error), "TLB-mapped LW executes");
    passed &= check(cpu.reg(2) == 0x0BADF00DULL,
                    "TLB-mapped LW reads translated physical page");

    if (!passed) return 1;
    std::cout << "All R4300i CPU tests passed.\n";
    return 0;
}
