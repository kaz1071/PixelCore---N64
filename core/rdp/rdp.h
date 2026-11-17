#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
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
    std::uint32_t blendColor() const;
    std::uint32_t fogColor() const;
    std::uint32_t primitiveColor() const;
    std::uint32_t environmentColor() const;
    std::uint16_t primitiveDepth() const;
    std::uint16_t primitiveDeltaZ() const;
    std::int16_t conversionCoefficient(std::size_t index) const;

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
    std::uint32_t blendColor_ = 0;
    std::uint32_t fogColor_ = 0;
    std::uint32_t primitiveColor_ = 0;
    std::uint32_t environmentColor_ = 0;
    std::uint16_t primitiveDepth_ = 0;
    std::uint16_t primitiveDeltaZ_ = 0;
    std::array<std::int16_t, 6> conversionCoefficients_{};
    std::uint32_t scissorLeft_ = 0;
    std::uint32_t scissorTop_ = 0;
    std::uint32_t scissorRight_ = 4096;
    std::uint32_t scissorBottom_ = 4096;
};