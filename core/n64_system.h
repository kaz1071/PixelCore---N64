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
    bool stepRsp(std::string &error);

    void loadCartridge(const std::vector<std::uint8_t> &image);
    void loadBootRom(const std::vector<std::uint8_t> &image);

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
};
