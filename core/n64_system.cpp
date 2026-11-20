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
    memory_.setSpProgramCounter(0);
    memory_.setSpHalted(true);
    memory_.setSpBroke(false);
    rspClockAccumulator_ = 0;
}

bool N64System::step(std::string &error)
{
    const bool result = cpu_.step(memory_, error);
    if (result)
        memory_.advanceViCycles(1);
    return result;
}

bool N64System::runInstructions(std::uint32_t cpuInstructionCount, std::string &error)
{
    // The R4300i runs at 93.75 MHz and the RSP at 62.5 MHz. This accumulator
    // schedules two RSP instructions for every three CPU instruction steps.
    for (std::uint32_t i = 0; i < cpuInstructionCount; ++i) {
        if (!step(error))
            return false;

        rspClockAccumulator_ = static_cast<std::uint8_t>(rspClockAccumulator_ + 2);
        if (rspClockAccumulator_ >= 3) {
            rspClockAccumulator_ = static_cast<std::uint8_t>(rspClockAccumulator_ - 3);
            if (!memory_.spHalted() && !stepRsp(error))
                return false;
        }
    }
    return true;
}

void N64System::advanceCpuCycles(std::uint32_t cycles)
{
    memory_.advanceViCycles(cycles);
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

bool N64System::startCartridgeDirectBoot(std::string &error)
{
    reset();
    std::uint32_t entryPoint = 0;
    if (!memory_.prepareCartridgeDirectBoot(entryPoint, error))
        return false;

    // Approximate the standard IPL3 hand-off arguments. This bypasses PIF/CIC
    // initialization and is a development aid, not an authentic hardware boot.
    cpu_.setReg(4, 0x80371240ULL); // a0: N64 boot signature
    cpu_.setReg(5, 0);             // a1: TV type placeholder
    cpu_.setReg(6, 0x000003F0ULL); // a2: memory-size argument used by IPL3
    cpu_.setReg(7, 0);             // a3: reset type placeholder
    cpu_.setPc(entryPoint);
    error.clear();
    return true;
}

R4300i &N64System::cpu() { return cpu_; }
const R4300i &N64System::cpu() const { return cpu_; }
Rsp &N64System::rsp() { return rsp_; }
const Rsp &N64System::rsp() const { return rsp_; }
N64MemoryBus &N64System::memory() { return memory_; }
const N64MemoryBus &N64System::memory() const { return memory_; }
