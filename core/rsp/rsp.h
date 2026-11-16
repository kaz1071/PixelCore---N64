#pragma once

#include <array>
#include <cstdint>
#include <string>

class N64Memory;

// Initial RSP processor core. Scalar instructions are implemented; vector
// registers are exposed for the vector execution unit being built incrementally.
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

    // Vector lanes are numbered 0..7, with lane 0 the most-significant
    // 16-bit element in the architectural 128-bit register.
    std::uint16_t vectorLane(unsigned int index, unsigned int lane) const;
    void setVectorLane(unsigned int index, unsigned int lane, std::uint16_t value);
    void setPc(std::uint32_t address);
    void resume();
    bool halted() const;
    bool broke() const;

private:
    bool execute(std::uint32_t instruction, N64Memory &memory,
                 std::string &error);
    static std::uint32_t signExtend16(std::uint32_t value);

    std::array<std::uint32_t, 32> registers_{};
    std::array<std::array<std::uint16_t, 8>, 32> vectorRegisters_{};
    std::uint16_t vectorCarry_ = 0;
    std::uint16_t vectorCompare_ = 0;
    std::uint8_t vectorClip_ = 0;
    std::uint32_t pc_ = 0;
    std::uint32_t nextPc_ = 4;
    std::uint32_t hi_ = 0;
    std::uint32_t lo_ = 0;
    bool delaySlotPending_ = false;
    bool halted_ = false;
    bool broke_ = false;
};
