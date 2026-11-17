#include "core/rdp/rdp.h"

#include <algorithm>

void Rdp::reset()
{
    pending_.clear();
    lastCommand_.clear();
    expectedWords_ = 0;
    completedCommands_ = 0;
    lastOpcode_ = 0;
    colorImageAddress_ = 0;
    colorImageWidth_ = 0;
    colorImageFormat_ = 0;
    colorImageSize_ = 0;
    fillColor_ = 0;
    otherModesHigh_ = 0;
    otherModesLow_ = 0;
    blendColor_ = 0;
    fogColor_ = 0;
    primitiveColor_ = 0;
    primitiveMinLevel_ = 0;
    primitiveLodFraction_ = 0;
    environmentColor_ = 0;
    primitiveDepth_ = 0;
    primitiveDeltaZ_ = 0;
    conversionCoefficients_.fill(0);
    tiles_.fill(RdpTileState{});
    scissorLeft_ = 0;
    scissorTop_ = 0;
    scissorRight_ = 4096;
    scissorBottom_ = 4096;
}

std::size_t Rdp::commandWordCount(std::uint8_t opcode)
{
    // Every triangle starts with four edge words. Optional coefficient
    // blocks add eight shade words, eight texture words, and two depth
    // words, respectively (all counts are 64-bit command words).
    constexpr std::size_t triangleWords[8] = {
        4, 12, 12, 20, 6, 14, 14, 22
    };

    if (opcode >= 0x08 && opcode <= 0x0F)
        return triangleWords[opcode - 0x08];

    // Texture rectangle and flipped texture rectangle include one
    // additional 64-bit parameter word.
    if (opcode == 0x24 || opcode == 0x25)
        return 2;

    // All other currently recognized single-word RDP commands use one
    // 64-bit command word. Unknown opcodes are safely framed as one word.
    return 1;
}

void Rdp::executeLastCommand(std::vector<std::uint8_t> &rdram)
{
    if (lastCommand_.empty())
        return;

    const std::uint64_t command = lastCommand_[0];
    switch (lastOpcode_)
    {
    case 0x3F: // SetColorImage
        colorImageFormat_ = static_cast<std::uint8_t>((command >> 53) & 0x07U);
        colorImageSize_ = static_cast<std::uint8_t>((command >> 51) & 0x03U);
        colorImageWidth_ = static_cast<std::uint32_t>((command >> 32) & 0x03FFU) + 1U;
        colorImageAddress_ = static_cast<std::uint32_t>(command & 0x03FFFFFFU);
        break;

    case 0x2D: // SetScissor (10.2 fixed-point coordinates)
        scissorLeft_ = (static_cast<std::uint32_t>((command >> 44) & 0x0FFFU) + 3U) >> 2;
        scissorTop_ = (static_cast<std::uint32_t>((command >> 32) & 0x0FFFU) + 3U) >> 2;
        scissorRight_ = static_cast<std::uint32_t>((command >> 12) & 0x0FFFU) >> 2;
        scissorBottom_ = static_cast<std::uint32_t>(command & 0x0FFFU) >> 2;
        break;

    case 0x37: // SetFillColor
        fillColor_ = static_cast<std::uint32_t>(command);
        break;

    case 0x35: // SetTile
    {
        const std::size_t tileIndex = static_cast<std::size_t>((command >> 24) & 0x07U);
        RdpTileState &tileState = tiles_[tileIndex];
        tileState.format = static_cast<std::uint8_t>((command >> 53) & 0x07U);
        tileState.size = static_cast<std::uint8_t>((command >> 51) & 0x03U);
        tileState.line = static_cast<std::uint16_t>((command >> 41) & 0x01FFU);
        tileState.tmem = static_cast<std::uint16_t>((command >> 32) & 0x01FFU);
        tileState.palette = static_cast<std::uint8_t>((command >> 20) & 0x0FU);
        const std::uint8_t modeT = static_cast<std::uint8_t>((command >> 18) & 0x03U);
        const std::uint8_t modeS = static_cast<std::uint8_t>((command >> 8) & 0x03U);
        tileState.clampT = static_cast<std::uint8_t>((modeT >> 1) & 1U);
        tileState.mirrorT = static_cast<std::uint8_t>(modeT & 1U);
        tileState.clampS = static_cast<std::uint8_t>((modeS >> 1) & 1U);
        tileState.mirrorS = static_cast<std::uint8_t>(modeS & 1U);
        tileState.maskT = static_cast<std::uint8_t>((command >> 14) & 0x0FU);
        tileState.shiftT = static_cast<std::uint8_t>((command >> 10) & 0x0FU);
        tileState.maskS = static_cast<std::uint8_t>((command >> 4) & 0x0FU);
        tileState.shiftS = static_cast<std::uint8_t>(command & 0x0FU);
        break;
    }

    case 0x2C: // SetConvert (six signed 9-bit coefficients)
        for (std::size_t i = 0; i < conversionCoefficients_.size(); ++i)
        {
            const unsigned shift = static_cast<unsigned>((5U - i) * 9U);
            const std::uint16_t raw = static_cast<std::uint16_t>((command >> shift) & 0x1FFU);
            conversionCoefficients_[i] = static_cast<std::int16_t>(
                (raw & 0x100U) ? static_cast<int>(raw) - 0x200 : raw);
        }
        break;

    case 0x2E: // SetPrimDepth
        primitiveDepth_ = static_cast<std::uint16_t>((command >> 16) & 0xFFFFU);
        primitiveDeltaZ_ = static_cast<std::uint16_t>(command & 0xFFFFU);
        break;

    case 0x2F: // SetOtherModes
        otherModesHigh_ = static_cast<std::uint32_t>((command >> 32) & 0x00FFFFFFU);
        otherModesLow_ = static_cast<std::uint32_t>(command);
        break;

    case 0x38: // SetFogColor
        fogColor_ = static_cast<std::uint32_t>(command);
        break;

    case 0x39: // SetBlendColor
        blendColor_ = static_cast<std::uint32_t>(command);
        break;

    case 0x3A: // SetPrimitiveColor
        primitiveMinLevel_ = static_cast<std::uint8_t>((command >> 40) & 0xFFU);
        primitiveLodFraction_ = static_cast<std::uint8_t>((command >> 32) & 0xFFU);
        primitiveColor_ = static_cast<std::uint32_t>(command);
        break;

    case 0x3B: // SetEnvironmentColor
        environmentColor_ = static_cast<std::uint32_t>(command);
        break;

    case 0x36: // FillRectangle
    {
        const std::uint32_t xStart = static_cast<std::uint32_t>((command >> 44) & 0x0FFFU) >> 2;
        const std::uint32_t yStart = static_cast<std::uint32_t>((command >> 32) & 0x0FFFU) >> 2;
        const std::uint32_t xEnd = static_cast<std::uint32_t>((command >> 12) & 0x0FFFU) >> 2;
        const std::uint32_t yEnd = static_cast<std::uint32_t>(command & 0x0FFFU) >> 2;
        const std::size_t bytesPerPixel = colorImageSize_ == 2 ? 2U : 4U;
        if (colorImageFormat_ == 0 && colorImageWidth_ != 0 &&
            (colorImageSize_ == 2 || colorImageSize_ == 3))
        {
            const std::uint32_t clippedLeft = std::min(std::max(xStart, scissorLeft_), colorImageWidth_);
            const std::uint32_t clippedRight = std::min({xEnd, scissorRight_, colorImageWidth_});
            const std::uint32_t clippedTop = std::max(yStart, scissorTop_);
            const std::uint32_t clippedBottom = std::min(yEnd, scissorBottom_);
            for (std::uint32_t y = clippedTop; y < clippedBottom; ++y)
                for (std::uint32_t x = clippedLeft; x < clippedRight; ++x)
                {
                    const std::uint64_t offset = static_cast<std::uint64_t>(colorImageAddress_) +
                        (static_cast<std::uint64_t>(y) * colorImageWidth_ + x) * bytesPerPixel;
                    if (offset > rdram.size() || bytesPerPixel > rdram.size() - offset) continue;
                    if (bytesPerPixel == 4)
                    {
                        rdram[offset] = static_cast<std::uint8_t>(fillColor_ >> 24);
                        rdram[offset + 1] = static_cast<std::uint8_t>(fillColor_ >> 16);
                        rdram[offset + 2] = static_cast<std::uint8_t>(fillColor_ >> 8);
                        rdram[offset + 3] = static_cast<std::uint8_t>(fillColor_);
                    }
                    else
                    {
                        const auto pixel = static_cast<std::uint16_t>((x & 1U) ? fillColor_ : fillColor_ >> 16);
                        rdram[offset] = static_cast<std::uint8_t>(pixel >> 8);
                        rdram[offset + 1] = static_cast<std::uint8_t>(pixel);
                    }
                }
        }
        break;
    }

    case 0x08: // FillTriangle (edge-only triangle)
    {
        if (colorImageFormat_ != 0 || colorImageWidth_ == 0 ||
            (colorImageSize_ != 2 && colorImageSize_ != 3) || lastCommand_.size() != 4)
            break;

        const std::uint64_t w0 = lastCommand_[0];
        const std::uint64_t w1 = lastCommand_[1];
        const std::uint64_t w2 = lastCommand_[2];
        const std::uint64_t w3 = lastCommand_[3];
        // Triangle Y coordinates are packed low-to-high as YH, YM, YL.
        const int yh = static_cast<int>(w0 & 0x3FFFU);
        const int ym = static_cast<int>((w0 >> 16) & 0x3FFFU);
        const int yl = static_cast<int>((w0 >> 32) & 0x3FFFU);
        const auto highSigned = [](std::uint64_t value) {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(value >> 32));
        };
        const auto lowSigned = [](std::uint64_t value) {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(value));
        };
        // Edge words are ordered XL/DXLDY, XH/DXHDY, XM/DXMDY.
        // XH is the long edge; the minor edge changes from XM to XL at YM.
        const std::int64_t xl = highSigned(w1);
        const std::int64_t dxldy = lowSigned(w1);
        const std::int64_t xh = highSigned(w2);
        const std::int64_t dxhdy = lowSigned(w2);
        const std::int64_t xm = highSigned(w3);
        const std::int64_t dxmdy = lowSigned(w3);
        const int firstY = static_cast<int>(std::max<std::uint32_t>(yh >> 2, scissorTop_));
        const int middleY = std::max(0, ym >> 2);
        const int lastY = static_cast<int>(std::min<std::uint32_t>((yl + 3) >> 2, scissorBottom_));
        const std::size_t bpp = colorImageSize_ == 2 ? 2U : 4U;

        for (int y = firstY; y < lastY; ++y)
        {
            const std::int64_t major = xh + dxhdy * (y - (yh >> 2));
            const bool lowerHalf = y >= middleY;
            // The minor edge starts at XM/YH, reaches the middle vertex,
            // then continues from XM/YM toward XL/YL with the lower slope.
            const std::int64_t minor = lowerHalf
                ? xm + dxldy * (y - middleY)
                : xm + dxmdy * (y - (yh >> 2));
            const int left = static_cast<int>(std::max<std::int64_t>(scissorLeft_, std::min(major, minor) >> 16));
            const int right = static_cast<int>(std::min<std::int64_t>(std::min(colorImageWidth_, scissorRight_),
                std::max(major, minor) >> 16));
            for (int x = left; x < right; ++x)
            {
                const std::uint64_t offset = static_cast<std::uint64_t>(colorImageAddress_) +
                    (static_cast<std::uint64_t>(y) * colorImageWidth_ + static_cast<unsigned>(x)) * bpp;
                if (offset > rdram.size() || bpp > rdram.size() - offset) continue;
                if (bpp == 4)
                {
                    rdram[offset] = static_cast<std::uint8_t>(fillColor_ >> 24);
                    rdram[offset + 1] = static_cast<std::uint8_t>(fillColor_ >> 16);
                    rdram[offset + 2] = static_cast<std::uint8_t>(fillColor_ >> 8);
                    rdram[offset + 3] = static_cast<std::uint8_t>(fillColor_);
                }
                else
                {
                    const auto pixel = static_cast<std::uint16_t>((x & 1) ? fillColor_ : fillColor_ >> 16);
                    rdram[offset] = static_cast<std::uint8_t>(pixel >> 8);
                    rdram[offset + 1] = static_cast<std::uint8_t>(pixel);
                }
            }
        }
        break;
    }

    default:
        break;
    }
}

bool Rdp::pushCommandWord(std::uint64_t word)
{
    if (pending_.empty())
    {
        lastOpcode_ = static_cast<std::uint8_t>((word >> 56) & 0x3FU);
        expectedWords_ = commandWordCount(lastOpcode_);
    }

    pending_.push_back(word);
    if (pending_.size() < expectedWords_)
        return false;

    lastCommand_ = pending_;
    pending_.clear();
    expectedWords_ = 0;
    ++completedCommands_;
    return true;
}

std::uint64_t Rdp::completedCommands() const
{
    return completedCommands_;
}

std::uint8_t Rdp::lastOpcode() const
{
    return lastOpcode_;
}

std::size_t Rdp::lastWordCount() const
{
    return lastCommand_.size();
}

const std::vector<std::uint64_t> &Rdp::lastCommand() const
{
    return lastCommand_;
}
std::uint32_t Rdp::otherModesHigh() const
{
    return otherModesHigh_;
}

std::uint32_t Rdp::otherModesLow() const
{
    return otherModesLow_;
}

std::uint8_t Rdp::cycleType() const
{
    return static_cast<std::uint8_t>((otherModesHigh_ >> 20) & 0x03U);
}

std::uint32_t Rdp::blendColor() const
{
    return blendColor_;
}

std::uint32_t Rdp::fogColor() const
{
    return fogColor_;
}

std::uint32_t Rdp::primitiveColor() const
{
    return primitiveColor_;
}

std::uint8_t Rdp::primitiveMinLevel() const
{
    return primitiveMinLevel_;
}

std::uint8_t Rdp::primitiveLodFraction() const
{
    return primitiveLodFraction_;
}

std::uint32_t Rdp::environmentColor() const
{
    return environmentColor_;
}

std::uint16_t Rdp::primitiveDepth() const
{
    return primitiveDepth_;
}

std::uint16_t Rdp::primitiveDeltaZ() const
{
    return primitiveDeltaZ_;
}

std::int16_t Rdp::conversionCoefficient(std::size_t index) const
{
    return index < conversionCoefficients_.size() ? conversionCoefficients_[index] : 0;
}

const RdpTileState &Rdp::tile(std::size_t index) const
{
    static const RdpTileState emptyTile{};
    return index < tiles_.size() ? tiles_[index] : emptyTile;
}
