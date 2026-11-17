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
}

std::size_t Rdp::commandWordCount(std::uint8_t opcode)
{
    // Triangle packets contain edge data followed by optional shade,
    // texture, and depth interpolation coefficients.
    constexpr std::size_t triangleWords[8] = {
        4, 6, 6, 8, 6, 8, 8, 10
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

    case 0x37: // SetFillColor
        fillColor_ = static_cast<std::uint32_t>(command);
        break;

    case 0x36: // FillRectangle
    {
        // This first rasterizer supports RGBA16 and RGBA32 color images.
        if (colorImageFormat_ != 0 || colorImageWidth_ == 0 ||
            (colorImageSize_ != 2 && colorImageSize_ != 3))
            break;

        const std::uint32_t xStart = static_cast<std::uint32_t>((command >> 44) & 0x0FFFU) >> 2;
        const std::uint32_t yStart = static_cast<std::uint32_t>((command >> 32) & 0x0FFFU) >> 2;
        const std::uint32_t xEnd = static_cast<std::uint32_t>((command >> 12) & 0x0FFFU) >> 2;
        const std::uint32_t yEnd = static_cast<std::uint32_t>(command & 0x0FFFU) >> 2;
        const std::uint32_t left = std::min(xStart, colorImageWidth_);
        const std::uint32_t right = std::min(xEnd, colorImageWidth_);
        const std::size_t bytesPerPixel = colorImageSize_ == 2 ? 2U : 4U;

        for (std::uint32_t y = yStart; y < yEnd; ++y)
        {
            for (std::uint32_t x = left; x < right; ++x)
            {
                const std::uint64_t offset =
                    static_cast<std::uint64_t>(colorImageAddress_) +
                    (static_cast<std::uint64_t>(y) * colorImageWidth_ + x) * bytesPerPixel;
                if (offset > rdram.size() || bytesPerPixel > rdram.size() - offset)
                    continue;

                if (bytesPerPixel == 4)
                {
                    rdram[static_cast<std::size_t>(offset)] =
                        static_cast<std::uint8_t>(fillColor_ >> 24);
                    rdram[static_cast<std::size_t>(offset + 1)] =
                        static_cast<std::uint8_t>(fillColor_ >> 16);
                    rdram[static_cast<std::size_t>(offset + 2)] =
                        static_cast<std::uint8_t>(fillColor_ >> 8);
                    rdram[static_cast<std::size_t>(offset + 3)] =
                        static_cast<std::uint8_t>(fillColor_);
                }
                else
                {
                    const std::uint16_t pixel = static_cast<std::uint16_t>(
                        (x & 1U) ? fillColor_ : (fillColor_ >> 16));
                    rdram[static_cast<std::size_t>(offset)] =
                        static_cast<std::uint8_t>(pixel >> 8);
                    rdram[static_cast<std::size_t>(offset + 1)] =
                        static_cast<std::uint8_t>(pixel);
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