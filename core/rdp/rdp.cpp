#include "core/rdp/rdp.h"

void Rdp::reset()
{
    pending_.clear();
    lastCommand_.clear();
    expectedWords_ = 0;
    completedCommands_ = 0;
    lastOpcode_ = 0;
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