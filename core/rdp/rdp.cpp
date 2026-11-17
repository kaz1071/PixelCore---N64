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