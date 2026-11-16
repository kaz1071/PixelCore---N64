#include "core/n64_system.h"

N64System::N64System()
{
    reset();
}

void N64System::reset()
{
    memory_.clearRdram();
    memory_.clearPifRam();
    cpu_.reset();
    rsp_.reset();
}

bool N64System::step(std::string &error)
{
    return cpu_.step(memory_, error);
}

bool N64System::stepRsp(std::string &error)
{
    if (memory_.spHalted()) {
        error = "RSP is halted";
        return false;
    }

    if (rsp_.pc() != memory_.spProgramCounter())
        rsp_.setPc(memory_.spProgramCounter());
    if (rsp_.halted())
        rsp_.resume();

    const bool result = rsp_.step(memory_, error);
    memory_.setSpProgramCounter(rsp_.pc());
    if (rsp_.halted()) {
        memory_.setSpHalted(true);
        memory_.setSpBroke(rsp_.broke());
    }
    return result;
}

void N64System::loadCartridge(const std::vector<std::uint8_t> &image)
{
    memory_.loadCartridge(image);
}

void N64System::loadBootRom(const std::vector<std::uint8_t> &image)
{
    memory_.loadBootRom(image);
}

R4300i &N64System::cpu() { return cpu_; }
const R4300i &N64System::cpu() const { return cpu_; }
Rsp &N64System::rsp() { return rsp_; }
const Rsp &N64System::rsp() const { return rsp_; }
N64MemoryBus &N64System::memory() { return memory_; }
const N64MemoryBus &N64System::memory() const { return memory_; }
