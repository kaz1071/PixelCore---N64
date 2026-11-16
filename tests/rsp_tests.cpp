#include "core/rsp/rsp.h"
#include "core/memory/n64_memory_bus.h"

#include <cstdint>
#include <iostream>
#include <string>

static bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

static std::uint32_t iType(unsigned op, unsigned rs, unsigned rt, std::uint16_t imm)
{
    return (op << 26) | (rs << 21) | (rt << 16) | imm;
}

static std::uint32_t rType(unsigned rs, unsigned rt, unsigned rd, unsigned sa, unsigned fn)
{
    return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | fn;
}

static std::uint32_t vectorType(unsigned element, unsigned vt, unsigned vs, unsigned vd, unsigned function)
{
    return (0x12U << 26) | (0x10U << 21) | ((element & 15U) << 21) |
           ((vt & 31U) << 16) | ((vs & 31U) << 11) |
           ((vd & 31U) << 6) | (function & 63U);
}

int main()
{
    bool passed = true;
    N64MemoryBus memory;
    Rsp rsp;
    std::string error;

    // addiu t0, zero, 12; addiu t1, t0, -3; addu t2, t0, t1; break
    const std::uint32_t program[] = {
        iType(0x09, 0, 8, 12),
        iType(0x09, 8, 9, 0xFFFD),
        rType(8, 9, 10, 0, 0x21),
        rType(0, 0, 0, 0, 0x0D)
    };
    for (unsigned i = 0; i < 4; ++i)
        passed &= check(memory.write32(N64MemoryBus::SpImemBase + i * 4, program[i]),
                        "write scalar RSP test instruction");

    passed &= check(rsp.step(memory, error), "RSP executes ADDIU");
    passed &= check(rsp.reg(8) == 12, "ADDIU updates scalar register");
    passed &= check(rsp.step(memory, error), "RSP executes signed ADDIU");
    passed &= check(rsp.reg(9) == 9, "ADDIU sign-extends immediate");
    passed &= check(rsp.step(memory, error), "RSP executes ADDU");
    passed &= check(rsp.reg(10) == 21, "ADDU combines scalar registers");
    passed &= check(rsp.step(memory, error), "RSP executes BREAK");
    passed &= check(rsp.halted(), "BREAK halts the RSP");
    passed &= check(rsp.reg(0) == 0, "RSP register zero remains hardwired");

    // Vector register storage exposes eight 16-bit lanes and clears on reset.
    rsp.setVectorLane(3, 0, 0x1234);
    rsp.setVectorLane(3, 7, 0xABCD);
    passed &= check(rsp.vectorLane(3, 0) == 0x1234 &&
                    rsp.vectorLane(3, 7) == 0xABCD,
                    "RSP vector register stores and reads endpoint lanes");
    rsp.setVectorLane(32, 0, 0xFFFF);
    rsp.setVectorLane(3, 8, 0xFFFF);
    passed &= check(rsp.vectorLane(32, 0) == 0 && rsp.vectorLane(3, 8) == 0,
                    "RSP vector lane access safely rejects invalid indices");
    rsp.reset();
    passed &= check(rsp.vectorLane(3, 0) == 0 && rsp.vectorLane(3, 7) == 0,
                    "RSP reset clears vector registers");

    // Basic RSP vector ALU operations and broadcast selection.
    rsp.reset();
    for (unsigned lane = 0; lane < 8; ++lane)
    {
        rsp.setVectorLane(1, lane, static_cast<std::uint16_t>(lane + 10));
        rsp.setVectorLane(2, lane, static_cast<std::uint16_t>(lane + 1));
    }
    memory.write32(N64MemoryBus::SpImemBase, vectorType(0, 2, 1, 3, 0x10)); // VADD
    passed &= check(rsp.step(memory, error), "RSP executes VADD");
    for (unsigned lane = 0; lane < 8; ++lane)
        passed &= check(rsp.vectorLane(3, lane) == lane + 11,
                        "VADD adds corresponding vector lanes");

    rsp.reset();
    for (unsigned lane = 0; lane < 8; ++lane)
    {
        rsp.setVectorLane(1, lane, static_cast<std::uint16_t>(lane + 20));
        rsp.setVectorLane(2, lane, static_cast<std::uint16_t>(3));
    }
    memory.write32(N64MemoryBus::SpImemBase, vectorType(8, 2, 1, 3, 0x11)); // VSUB vt[0]
    passed &= check(rsp.step(memory, error), "RSP executes broadcast VSUB");
    for (unsigned lane = 0; lane < 8; ++lane)
        passed &= check(rsp.vectorLane(3, lane) == lane + 17,
                        "VSUB broadcasts selected source lane");

    rsp.reset();
    rsp.setVectorLane(1, 0, 0xF0F0);
    rsp.setVectorLane(2, 0, 0x0FF0);
    memory.write32(N64MemoryBus::SpImemBase, vectorType(0, 2, 1, 3, 0x28)); // VAND
    passed &= check(rsp.step(memory, error) && rsp.vectorLane(3, 0) == 0x00F0,
                    "VAND computes lane-wise bitwise AND");
    memory.write32(N64MemoryBus::SpImemBase + 4, vectorType(0, 2, 1, 4, 0x2A)); // VOR
    passed &= check(rsp.step(memory, error) && rsp.vectorLane(4, 0) == 0xFFF0,
                    "VOR computes lane-wise bitwise OR");
    memory.write32(N64MemoryBus::SpImemBase + 8, vectorType(0, 2, 1, 5, 0x2C)); // VXOR
    passed &= check(rsp.step(memory, error) && rsp.vectorLane(5, 0) == 0xFF00,
                    "VXOR computes lane-wise bitwise XOR");

    rsp.reset();
    rsp.setVectorLane(1, 0, 32760);
    rsp.setVectorLane(2, 0, 100);
    memory.write32(N64MemoryBus::SpImemBase, vectorType(0, 2, 1, 3, 0x10));
    passed &= check(rsp.step(memory, error) && rsp.vectorLane(3, 0) == 32767,
                    "VADD saturates positive signed overflow");
    rsp.setVectorLane(1, 0, 0x8000);
    rsp.setVectorLane(2, 0, 1);
    rsp.setPc(0);
    passed &= check(rsp.step(memory, error) && rsp.vectorLane(3, 0) == 0x8000,
                    "VADD saturates negative signed overflow");

    rsp.reset();
    // Branch at 0, delay slot at 4, target at 12.
    memory.write32(N64MemoryBus::SpImemBase, iType(0x04, 0, 0, 2));
    memory.write32(N64MemoryBus::SpImemBase + 4, iType(0x09, 0, 8, 7));
    memory.write32(N64MemoryBus::SpImemBase + 8, iType(0x09, 0, 9, 99));
    memory.write32(N64MemoryBus::SpImemBase + 12, iType(0x09, 0, 10, 11));
    passed &= check(rsp.step(memory, error), "RSP executes taken BEQ");
    passed &= check(rsp.pc() == 4, "RSP enters branch delay slot");
    passed &= check(rsp.step(memory, error), "RSP executes branch delay slot");
    passed &= check(rsp.reg(8) == 7 && rsp.pc() == 12,
                    "RSP delay slot executes before branch target");
    passed &= check(rsp.step(memory, error), "RSP executes branch target");
    passed &= check(rsp.reg(10) == 11 && rsp.reg(9) == 0,
                    "RSP skips instruction after taken branch");

    rsp.reset();
    memory.write32(N64MemoryBus::SpImemBase, iType(0x0F, 0, 8, 0x1234));
    memory.write32(N64MemoryBus::SpImemBase + 4, iType(0x2B, 0, 8, 0x0100));
    memory.write32(N64MemoryBus::SpImemBase + 8, iType(0x23, 0, 9, 0x0100));
    passed &= check(rsp.step(memory, error), "RSP executes LUI");
    passed &= check(rsp.step(memory, error), "RSP executes SW to DMEM");
    passed &= check(rsp.step(memory, error), "RSP executes LW from DMEM");
    passed &= check(rsp.reg(9) == 0x12340000,
                    "RSP scalar loads and stores access DMEM");

    rsp.reset();
    // Signed multiply: 6 * 7 -> LO, then move LO into a GPR.
    memory.write32(N64MemoryBus::SpImemBase, iType(0x09, 0, 8, 6));
    memory.write32(N64MemoryBus::SpImemBase + 4, iType(0x09, 0, 9, 7));
    memory.write32(N64MemoryBus::SpImemBase + 8, rType(8, 9, 0, 0, 0x18));
    memory.write32(N64MemoryBus::SpImemBase + 12, rType(0, 0, 10, 0, 0x12));
    passed &= check(rsp.step(memory, error) && rsp.step(memory, error) &&
                    rsp.step(memory, error) && rsp.step(memory, error),
                    "RSP executes multiply and move-from-LO sequence");
    passed &= check(rsp.reg(10) == 42,
                    "RSP MULT and MFLO produce the expected product");

    rsp.reset();
    // BLTZ takes the branch, but its delay-slot instruction still executes.
    memory.write32(N64MemoryBus::SpImemBase, iType(0x09, 0, 8, 0xFFFF));
    memory.write32(N64MemoryBus::SpImemBase + 4, iType(0x01, 8, 0, 2));
    memory.write32(N64MemoryBus::SpImemBase + 8, iType(0x09, 0, 9, 1));
    memory.write32(N64MemoryBus::SpImemBase + 12, iType(0x09, 0, 10, 2));
    memory.write32(N64MemoryBus::SpImemBase + 16, iType(0x09, 0, 11, 3));
    passed &= check(rsp.step(memory, error) && rsp.step(memory, error) &&
                    rsp.step(memory, error) && rsp.step(memory, error),
                    "RSP executes REGIMM branch and delay slot");
    passed &= check(rsp.reg(9) == 1 && rsp.reg(10) == 0 && rsp.reg(11) == 3,
                    "RSP BLTZ selects the target after executing its delay slot");

    rsp.reset();
    // BNE uses the same PC-relative base and delay-slot rules.
    memory.write32(N64MemoryBus::SpImemBase, iType(0x09, 0, 8, 1));
    memory.write32(N64MemoryBus::SpImemBase + 4, iType(0x05, 8, 0, 2));
    memory.write32(N64MemoryBus::SpImemBase + 8, iType(0x09, 0, 9, 4));
    memory.write32(N64MemoryBus::SpImemBase + 12, iType(0x09, 0, 10, 5));
    memory.write32(N64MemoryBus::SpImemBase + 16, iType(0x09, 0, 11, 6));
    passed &= check(rsp.step(memory, error) && rsp.step(memory, error) &&
                    rsp.step(memory, error) && rsp.step(memory, error),
                    "RSP executes BNE and its delay slot");
    passed &= check(rsp.reg(9) == 4 && rsp.reg(10) == 0 && rsp.reg(11) == 6,
                    "RSP BNE branches to the correct target");

    if (!passed) return 1;
    std::cout << "All RSP scalar CPU tests passed.\n";
    return 0;
}
