#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// RDP command-stream decoder with an initial fill-rectangle rasterizer.
class Rdp
{
public:
    void reset();
    bool pushCommandWord(std::uint64_t word);
    void executeLastCommand(std::vector<std::uint8_t> &rdram);

    static std::size_t commandWordCount(std::uint8_t opcode);

    std::uint64_t completedCommands() const;
    std::uint8_t lastOpcode() const;
    std::size_t lastWordCount() const;
    const std::vector<std::uint64_t> &lastCommand() const;
    std::uint32_t otherModesHigh() const;
    std::uint32_t otherModesLow() const;
    std::uint8_t cycleType() const;

private:
    std::vector<std::uint64_t> pending_;
    std::vector<std::uint64_t> lastCommand_;
    std::size_t expectedWords_ = 0;
    std::uint64_t completedCommands_ = 0;
    std::uint8_t lastOpcode_ = 0;
    std::uint32_t colorImageAddress_ = 0;
    std::uint32_t colorImageWidth_ = 0;
    std::uint8_t colorImageFormat_ = 0;
    std::uint8_t colorImageSize_ = 0;
    std::uint32_t fillColor_ = 0;
    std::uint32_t otherModesHigh_ = 0;
    std::uint32_t otherModesLow_ = 0;
    std::uint32_t scissorLeft_ = 0;
    std::uint32_t scissorTop_ = 0;
    std::uint32_t scissorRight_ = 4096;
    std::uint32_t scissorBottom_ = 4096;
};