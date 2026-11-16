#pragma once

#include <array>
#include <cstdint>
#include <string>

class N64Memory;

// Initial scalar RSP processor core. Vector instructions and COP0 are not
// implemented yet; instruction and data accesses use the SP's 4 KiB memories.
class Rsp
{
public:
    static constexpr std::uint32_t InstructionMemoryBase = 0x04001000U;
    static constexpr std::uint32_t DataMemoryBase = 0x04000000U;

    void reset();
    bool step(N64Memory &memory, std::string &error);

    std::uint32_t pc() const;
    std::uint32_t reg(unsigned int index) const;
    void setReg(unsigned int index, std::uint32_t value);
    void setPc(std::uint32_t address);
    bool halted() const;
    bool broke() const;

private:
    bool execute(std::uint32_t instruction, N64Memory &memory,
                 std::string &error);
    static std::uint32_t signExtend16(std::uint32_t value);

    std::array<std::uint32_t, 32> registers_{};
    std::uint32_t pc_ = 0;
    std::uint32_t nextPc_ = 4;
    std::uint32_t hi_ = 0;
    std::uint32_t lo_ = 0;
    bool delaySlotPending_ = false;
    bool halted_ = false;
    bool broke_ = false;
};
