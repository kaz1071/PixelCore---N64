#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// RDP command-stream framing. Commands are transferred as 64-bit words;
// rendering operations will be implemented on top of this decoder.
class Rdp
{
public:
    void reset();
    bool pushCommandWord(std::uint64_t word);

    static std::size_t commandWordCount(std::uint8_t opcode);

    std::uint64_t completedCommands() const;
    std::uint8_t lastOpcode() const;
    std::size_t lastWordCount() const;
    const std::vector<std::uint64_t> &lastCommand() const;

private:
    std::vector<std::uint64_t> pending_;
    std::vector<std::uint64_t> lastCommand_;
    std::size_t expectedWords_ = 0;
    std::uint64_t completedCommands_ = 0;
    std::uint8_t lastOpcode_ = 0;
};