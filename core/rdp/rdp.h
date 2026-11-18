#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

struct RdpTileState
{
    std::uint8_t format = 0;
    std::uint8_t size = 0;
    std::uint16_t line = 0;
    std::uint16_t tmem = 0;
    std::uint8_t palette = 0;
    std::uint8_t clampT = 0;
    std::uint8_t mirrorT = 0;
    std::uint8_t maskT = 0;
    std::uint8_t shiftT = 0;
    std::uint8_t clampS = 0;
    std::uint8_t mirrorS = 0;
    std::uint8_t maskS = 0;
    std::uint8_t shiftS = 0;
    std::uint16_t upperLeftS = 0;
    std::uint16_t upperLeftT = 0;
    std::uint16_t lowerRightS = 0;
    std::uint16_t lowerRightT = 0;
    std::uint8_t loadType = 0;
    std::uint16_t loadStartS = 0;
    std::uint16_t loadStartT = 0;
    std::uint16_t loadEndS = 0;
    std::uint16_t loadEndT = 0;
    std::uint16_t loadDxt = 0;
    bool loaded = false;
};

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
    std::uint8_t textureFilterMode() const;
    std::uint32_t blendColor() const;
    std::uint32_t fogColor() const;
    std::uint32_t primitiveColor() const;
    std::uint8_t primitiveMinLevel() const;
    std::uint8_t primitiveLodFraction() const;
    std::uint32_t environmentColor() const;
    std::uint64_t combineMux0() const;
    std::uint64_t combineMux1() const;
    std::uint32_t depthImageAddress() const;
    std::uint16_t primitiveDepth() const;
    static std::uint16_t compressDepth(std::uint32_t depth);
    static std::uint32_t decompressDepth(std::uint16_t encodedDepth);
    std::uint16_t primitiveDeltaZ() const;
    std::int16_t conversionCoefficient(std::size_t index) const;
    const RdpTileState &tile(std::size_t index) const;
    std::uint8_t tmemByte(std::size_t index) const;
    // Returns a raw big-endian texel value after applying the tile's integer-coordinate addressing, or zero when unavailable.
    std::uint32_t textureTexel(std::size_t tileIndex, std::int64_t s, std::int64_t t) const;
    // Returns RGBA8888 for supported RGBA, YUV16, CI (when TLUT is enabled), IA, and intensity texels; zero for unsupported formats.
    std::uint32_t textureRgba(std::size_t tileIndex, std::int64_t s, std::int64_t t) const;
    // Bilinearly interpolates four RGBA texels using 5-bit fractional S/T coordinates.
    std::uint32_t textureRgbaBilinear(std::size_t tileIndex, std::int64_t s, std::int64_t t,
                                      std::uint8_t fractionS, std::uint8_t fractionT) const;
    // Selects point, bilinear, or four-texel average filtering from SetOtherModes.
    std::uint32_t textureRgbaFiltered(std::size_t tileIndex, std::int64_t s, std::int64_t t,
                                      std::uint8_t fractionS, std::uint8_t fractionT) const;

private:
    std::vector<std::uint64_t> pending_;
    std::vector<std::uint64_t> lastCommand_;
    std::size_t expectedWords_ = 0;
    std::uint64_t completedCommands_ = 0;
    std::uint8_t lastOpcode_ = 0;
    std::uint32_t textureImageAddress_ = 0;
    std::uint32_t textureImageWidth_ = 0;
    std::uint8_t textureImageSize_ = 0;
    std::array<std::uint8_t, 4096> tmem_{};
    std::uint32_t colorImageAddress_ = 0;
    std::uint32_t depthImageAddress_ = 0;
    std::uint32_t colorImageWidth_ = 0;
    std::uint8_t colorImageFormat_ = 0;
    std::uint8_t colorImageSize_ = 0;
    std::uint32_t fillColor_ = 0;
    std::uint32_t otherModesHigh_ = 0;
    std::uint32_t otherModesLow_ = 0;
    std::uint32_t blendColor_ = 0;
    std::uint32_t fogColor_ = 0;
    std::uint32_t primitiveColor_ = 0;
    std::uint8_t primitiveMinLevel_ = 0;
    std::uint8_t primitiveLodFraction_ = 0;
    std::uint32_t environmentColor_ = 0;
    std::uint64_t combineMux0_ = 0;
    std::uint64_t combineMux1_ = 0;
    std::uint16_t primitiveDepth_ = 0;
    std::uint16_t primitiveDeltaZ_ = 0;
    std::array<std::int16_t, 6> conversionCoefficients_{};
    std::array<RdpTileState, 8> tiles_{};
    std::uint32_t scissorLeft_ = 0;
    std::uint32_t scissorTop_ = 0;
    std::uint32_t scissorRight_ = 4096;
    std::uint32_t scissorBottom_ = 4096;
};