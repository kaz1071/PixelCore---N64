#pragma once

#include <array>
#include <cstdint>
#include <string>

struct R4300iTlbEntry
{
    std::uint64_t pageMask = 0;
    std::uint64_t entryHi = 0;
    std::uint64_t entryLo0 = 0;
    std::uint64_t entryLo1 = 0;
};

class N64Memory
{
public:
    virtual ~N64Memory() = default;
    virtual bool read32(std::uint64_t address, std::uint32_t &value) = 0;
    virtual bool write32(std::uint64_t address, std::uint32_t value) = 0;

    // Default big-endian helpers. Memory implementations may override these
    // when they have more efficient byte/halfword/doubleword access.
    virtual bool read8(std::uint64_t address, std::uint8_t &value);
    virtual bool read16(std::uint64_t address, std::uint16_t &value);
    virtual bool read64(std::uint64_t address, std::uint64_t &value);
    virtual bool write8(std::uint64_t address, std::uint8_t value);
    virtual bool write16(std::uint64_t address, std::uint16_t value);
    virtual bool write64(std::uint64_t address, std::uint64_t value);
    virtual bool interruptPending() const;
};

class R4300i
{
public:
    static constexpr std::uint64_t ResetAddress = 0xBFC00000ULL;
    static constexpr std::uint64_t GeneralExceptionVector = 0x80000180ULL;
    static constexpr std::uint64_t BootstrapExceptionVector = 0xBFC00380ULL;

    // CP0 register numbers used by the R4300i.
    enum Cp0Register : unsigned int {
        Index = 0, Random = 1, EntryLo0 = 2, EntryLo1 = 3,
        Context = 4, PageMask = 5, Wired = 6, BadVAddr = 8,
        Count = 9, EntryHi = 10, Compare = 11, Status = 12,
        Cause = 13, EPC = 14, PRId = 15, Config = 16,
        LLAddr = 17, XContext = 20, ErrorEPC = 30
    };

    void reset();
    bool step(N64Memory &memory, std::string &error);

    std::uint64_t reg(unsigned int index) const;
    void setReg(unsigned int index, std::uint64_t value);
    std::uint64_t pc() const;
    std::uint64_t hi() const;
    std::uint64_t lo() const;
    std::uint64_t cp0(unsigned int index) const;
    void setCp0(unsigned int index, std::uint64_t value);

private:
    bool execute(std::uint32_t instruction, std::uint64_t instructionAddress,
                 N64Memory &memory, std::string &error);
    void raiseException(unsigned int exceptionCode, std::uint64_t faultAddress,
                        bool hasBadAddress = false);
    bool translateAddress(std::uint64_t virtualAddress, bool isStore,
                          std::uint64_t &physicalAddress,
                          std::uint64_t instructionAddress);

    std::array<std::uint64_t, 32> registers_{};
    std::array<std::uint64_t, 32> cp0Registers_{};
    std::uint64_t pc_ = ResetAddress;
    std::uint64_t nextPc_ = ResetAddress + 4;
    std::uint64_t hi_ = 0;
    std::uint64_t lo_ = 0;
    bool nextInstructionIsDelaySlot_ = false;
    bool currentInstructionInDelaySlot_ = false;
    bool llBit_ = false;
    std::uint64_t llAddress_ = 0;
    std::array<R4300iTlbEntry, 32> tlb_{};
};
