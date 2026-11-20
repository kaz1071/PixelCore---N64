#pragma once

#include "core/cpu/r4300i.h"
#include "core/memory/n64_memory_bus.h"
#include "core/rsp/rsp.h"

#include <string>
#include <vector>

// Owns the emulated CPU and memory bus and connects instruction execution
// to the mapped N64 memory.
class N64System
{
public:
    N64System();

    void reset();
    bool step(std::string &error);
    // Run a bounded batch of CPU instructions and schedule the RSP at its
    // approximate 2:3 clock ratio. Returns false if CPU or RSP execution fails.
    bool runInstructions(std::uint32_t cpuInstructionCount, std::string &error);
    void advanceCpuCycles(std::uint32_t cycles);
    bool stepRsp(std::string &error);

    void loadCartridge(const std::vector<std::uint8_t> &image);
    void loadBootRom(const std::vector<std::uint8_t> &image);
    bool startCartridgeDirectBoot(std::string &error);

    R4300i &cpu();
    const R4300i &cpu() const;
    Rsp &rsp();
    const Rsp &rsp() const;
    N64MemoryBus &memory();
    const N64MemoryBus &memory() const;

private:
    N64MemoryBus memory_;
    R4300i cpu_;
    Rsp rsp_;
    std::uint8_t rspClockAccumulator_ = 0;
};
