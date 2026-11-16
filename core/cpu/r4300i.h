#pragma once

#include <array>
#include <cstdint>
#include <string>

class N64Memory
{
public:
    virtual ~N64Memory() = default;
    virtual bool read32(std::uint64_t address, std::uint32_t &value) = 0;
    virtual bool write32(std::uint64_t address, std::uint32_t value) = 0;
};

class R4300i
{
public:
    static constexpr std::uint64_t ResetAddress = 0xBFC00000ULL;

    void reset();
    bool step(N64Memory &memory, std::string &error);

    std::uint64_t reg(unsigned int index) const;
    void setReg(unsigned int index, std::uint64_t value);
    std::uint64_t pc() const;
    std::uint64_t hi() const;
    std::uint64_t lo() const;

private:
    bool execute(std::uint32_t instruction, std::uint64_t instructionAddress,
                 N64Memory &memory, std::string &error);

    std::array<std::uint64_t, 32> registers_{};
    std::uint64_t pc_ = ResetAddress;
    std::uint64_t nextPc_ = ResetAddress + 4;
    std::uint64_t hi_ = 0;
    std::uint64_t lo_ = 0;
};
