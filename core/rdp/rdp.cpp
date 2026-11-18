#include "core/rdp/rdp.h"

#include <algorithm>
#include <limits>

void Rdp::reset()
{
    pending_.clear();
    lastCommand_.clear();
    expectedWords_ = 0;
    completedCommands_ = 0;
    lastOpcode_ = 0;
    textureImageAddress_ = 0;
    textureImageWidth_ = 0;
    textureImageSize_ = 0;
    tmem_.fill(0);
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
    combineMux0_ = 0;
    combineMux1_ = 0;
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

    // SetCombine carries two 64-bit words containing the color/alpha muxes.
    if (opcode == 0x3C)
        return 2;

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
    case 0x3D: // SetTextureImage
        textureImageSize_ = static_cast<std::uint8_t>((command >> 51) & 0x03U);
        textureImageWidth_ = static_cast<std::uint32_t>((command >> 32) & 0x03FFU) + 1U;
        textureImageAddress_ = static_cast<std::uint32_t>(command & 0x03FFFFFFU);
        break;

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

    case 0x32: // SetTileSize
    {
        const std::size_t tileIndex = static_cast<std::size_t>((command >> 24) & 0x07U);
        RdpTileState &tileState = tiles_[tileIndex];
        tileState.upperLeftS = static_cast<std::uint16_t>((command >> 44) & 0x0FFFU);
        tileState.upperLeftT = static_cast<std::uint16_t>((command >> 32) & 0x0FFFU);
        tileState.lowerRightS = static_cast<std::uint16_t>((command >> 12) & 0x0FFFU);
        tileState.lowerRightT = static_cast<std::uint16_t>(command & 0x0FFFU);
        break;
    }

    case 0x30: // LoadTLUT
    {
        const std::size_t tileIndex = static_cast<std::size_t>((command >> 24) & 0x07U);
        RdpTileState &tileState = tiles_[tileIndex];
        tileState.loadType = 3;
        tileState.loadStartS = static_cast<std::uint16_t>((command >> 44) & 0x0FFFU);
        tileState.loadStartT = static_cast<std::uint16_t>((command >> 32) & 0x0FFFU);
        tileState.loadEndS = static_cast<std::uint16_t>((command >> 12) & 0x0FFFU);
        tileState.loadEndT = tileState.loadStartT;
        tileState.loadDxt = 0;
        tileState.loaded = false;

        if (textureImageWidth_ == 0 || tileState.loadEndS < tileState.loadStartS ||
            textureImageSize_ != 2U)
            break;

        const std::size_t entryCount =
            (static_cast<std::size_t>(tileState.loadEndS - tileState.loadStartS) >> 2) + 1U;
        const std::size_t sourceS = static_cast<std::size_t>(tileState.loadStartS >> 2);
        const std::size_t sourceT = static_cast<std::size_t>(tileState.loadStartT >> 2);
        const std::size_t destinationBase = static_cast<std::size_t>(tileState.tmem) * 8U;

        // Validate the complete transfer before touching TMEM. A failed load
        // must not leave a partially updated palette or report the tile loaded.
        if (sourceS >= textureImageWidth_ ||
            entryCount > textureImageWidth_ - sourceS ||
            entryCount > (tmem_.size() - std::min(destinationBase, tmem_.size())) / 8U ||
            destinationBase > tmem_.size())
            break;

        const std::uint64_t sourceTexel =
            static_cast<std::uint64_t>(sourceT) * textureImageWidth_ + sourceS;
        const std::uint64_t sourceAddress =
            static_cast<std::uint64_t>(textureImageAddress_) + sourceTexel * 2U;
        const std::uint64_t sourceBytes = static_cast<std::uint64_t>(entryCount) * 2U;
        if (sourceAddress > rdram.size() || sourceBytes > rdram.size() - sourceAddress)
            break;

        for (std::size_t entry = 0; entry < entryCount; ++entry)
        {
            const std::uint8_t high = rdram[static_cast<std::size_t>(sourceAddress) + entry * 2U];
            const std::uint8_t low = rdram[static_cast<std::size_t>(sourceAddress) + entry * 2U + 1U];
            const std::size_t destinationAddress = destinationBase + entry * 8U;
            // Each 16-bit palette entry is replicated into four TMEM banks.
            for (std::size_t bank = 0; bank < 4; ++bank)
            {
                tmem_[destinationAddress + bank * 2U] = high;
                tmem_[destinationAddress + bank * 2U + 1U] = low;
            }
        }
        tileState.loaded = true;
        break;
    }

case 0x33: // LoadBlock
    {
        const std::size_t tileIndex = static_cast<std::size_t>((command >> 24) & 0x07U);
        RdpTileState &tileState = tiles_[tileIndex];
        tileState.loadType = 1;
        tileState.loadStartS = static_cast<std::uint16_t>((command >> 44) & 0x0FFFU);
        tileState.loadStartT = static_cast<std::uint16_t>((command >> 32) & 0x0FFFU);
        tileState.loadEndS = static_cast<std::uint16_t>((command >> 12) & 0x0FFFU);
        tileState.loadEndT = 0;
        tileState.loadDxt = static_cast<std::uint16_t>(command & 0x0FFFU);
        tileState.loaded = false;

        if (textureImageWidth_ == 0 || tileState.loadEndS < tileState.loadStartS)
            break;

        const std::size_t bitsPerTexel = 4U << textureImageSize_;
        const std::size_t texelCount =
            (static_cast<std::size_t>(tileState.loadEndS - tileState.loadStartS) >> 2) + 1U;
        const std::size_t sourceRowBytes =
            (static_cast<std::size_t>(textureImageWidth_) * bitsPerTexel + 7U) / 8U;
        const std::size_t sourceStartS = tileState.loadStartS >> 2;
        const std::size_t sourceStartT = tileState.loadStartT >> 2;
        const std::size_t destinationBase = static_cast<std::size_t>(tileState.tmem) * 8U;
        std::size_t destinationBit = 0;
        std::size_t row = 0;
        std::size_t dxtAccumulator = 0;
        const std::size_t rowStrideBits = tileState.line != 0
            ? static_cast<std::size_t>(tileState.line) * 64U
            : ((texelCount * bitsPerTexel + 63U) & ~std::size_t(63U));

        // Validate the complete linear source run and every DXT-selected
        // TMEM destination before writing. A truncated LoadBlock is rejected
        // atomically rather than leaving a partially initialized tile.
        bool validTransfer = true;
        {
            std::size_t checkDestinationBit = 0;
            std::size_t checkRow = 0;
            std::size_t checkDxtAccumulator = 0;
            for (std::size_t texel = 0; texel < texelCount; ++texel)
            {
                const std::size_t sourceLinearTexel =
                    sourceStartT * textureImageWidth_ + sourceStartS + texel;
                const std::size_t sourceRow = sourceLinearTexel / textureImageWidth_;
                const std::size_t sourceTexel = sourceLinearTexel % textureImageWidth_;
                const std::uint64_t sourceRowAddress =
                    static_cast<std::uint64_t>(textureImageAddress_) + sourceRow * sourceRowBytes;
                const std::uint64_t sourceBit =
                    static_cast<std::uint64_t>(sourceTexel) * bitsPerTexel;
                const std::uint64_t sourceAddress = sourceRowAddress + sourceBit / 8U;
                const std::size_t destinationAddress = destinationBase +
                    ((checkDestinationBit / 8U) ^ ((checkRow & 1U) != 0U ? 4U : 0U));
                const std::size_t texelBytes = bitsPerTexel == 4U ? 1U : bitsPerTexel / 8U;
                if (sourceAddress > rdram.size() ||
                    texelBytes > rdram.size() - sourceAddress ||
                    destinationAddress > tmem_.size() ||
                    texelBytes > tmem_.size() - destinationAddress)
                {
                    validTransfer = false;
                    break;
                }

                checkDestinationBit += bitsPerTexel;
                if ((checkDestinationBit & 63U) == 0U)
                {
                    checkDxtAccumulator += tileState.loadDxt;
                    const std::size_t dxtRow = checkDxtAccumulator >> 11;
                    if (dxtRow > checkRow)
                    {
                        checkRow = dxtRow;
                        if (checkRow > std::numeric_limits<std::size_t>::max() / rowStrideBits)
                        {
                            validTransfer = false;
                            break;
                        }
                        checkDestinationBit = checkRow * rowStrideBits;
                    }
                }
            }
        }
        if (!validTransfer)
            break;

        for (std::size_t texel = 0; texel < texelCount; ++texel)
        {
            const std::size_t sourceLinearTexel =
                sourceStartT * textureImageWidth_ + sourceStartS + texel;
            const std::size_t sourceRow = sourceLinearTexel / textureImageWidth_;
            const std::size_t sourceTexel = sourceLinearTexel % textureImageWidth_;
            const std::uint64_t sourceRowAddress = static_cast<std::uint64_t>(textureImageAddress_) +
                sourceRow * sourceRowBytes;
            const std::uint64_t sourceBit = static_cast<std::uint64_t>(sourceTexel) * bitsPerTexel;
            const std::uint64_t sourceAddress = sourceRowAddress + sourceBit / 8U;
            if (bitsPerTexel == 4U)
            {
                const std::size_t destinationAddress = destinationBase +
                    ((destinationBit / 8U) ^ ((row & 1U) != 0U ? 4U : 0U));
                const unsigned sourceShift = (sourceBit & 4U) ? 0U : 4U;
                const unsigned destinationShift = (destinationBit & 4U) ? 0U : 4U;
                const std::uint8_t nibble = static_cast<std::uint8_t>((rdram[sourceAddress] >> sourceShift) & 0x0FU);
                tmem_[destinationAddress] = static_cast<std::uint8_t>(
                    (tmem_[destinationAddress] & ~(0x0FU << destinationShift)) |
                    (nibble << destinationShift));
            }
            else
            {
                const std::size_t texelBytes = bitsPerTexel / 8U;
                const std::size_t destinationAddress = destinationBase +
                    ((destinationBit / 8U) ^ ((row & 1U) != 0U ? 4U : 0U));
                for (std::size_t byte = 0; byte < texelBytes; ++byte)
                    tmem_[destinationAddress + byte] = rdram[sourceAddress + byte];
            }

            destinationBit += bitsPerTexel;
            if ((destinationBit & 63U) == 0U)
            {
                dxtAccumulator += tileState.loadDxt;
                const std::size_t dxtRow = dxtAccumulator >> 11;
                if (dxtRow > row)
                {
                    row = dxtRow;
                    destinationBit = row * rowStrideBits;
                }
            }
        }
        tileState.loaded = true;
        break;
    }
    case 0x34: // LoadTile
    {
        const std::size_t tileIndex = static_cast<std::size_t>((command >> 24) & 0x07U);
        RdpTileState &tileState = tiles_[tileIndex];
        tileState.loadType = 2;
        tileState.loadStartS = static_cast<std::uint16_t>((command >> 44) & 0x0FFFU);
        tileState.loadStartT = static_cast<std::uint16_t>((command >> 32) & 0x0FFFU);
        tileState.loadEndS = static_cast<std::uint16_t>((command >> 12) & 0x0FFFU);
        tileState.loadEndT = static_cast<std::uint16_t>(command & 0x0FFFU);
        tileState.loadDxt = 0;
        tileState.loaded = false;

        if (textureImageWidth_ == 0 || tileState.loadEndS < tileState.loadStartS ||
            tileState.loadEndT < tileState.loadStartT)
            break;

        const std::size_t bitsPerTexel = 4U << textureImageSize_;
        const std::size_t sourceRowBytes =
            (static_cast<std::size_t>(textureImageWidth_) * bitsPerTexel + 7U) / 8U;
        const std::size_t rowTexels =
            (static_cast<std::size_t>(tileState.loadEndS - tileState.loadStartS) >> 2) + 1U;
        const std::size_t rowCount =
            (static_cast<std::size_t>(tileState.loadEndT - tileState.loadStartT) >> 2) + 1U;
        const std::size_t rowBits = rowTexels * bitsPerTexel;
        const std::size_t rowBytes = (rowBits + 7U) / 8U;
        const std::size_t destinationStride = tileState.line != 0
            ? static_cast<std::size_t>(tileState.line) * 8U
            : ((rowBytes + 7U) & ~std::size_t(7U));
        const std::size_t destinationBase = static_cast<std::size_t>(tileState.tmem) * 8U;
        const std::size_t sourceStartS = tileState.loadStartS >> 2;
        const std::size_t sourceStartT = tileState.loadStartT >> 2;

        // Validate every source and destination texel before changing TMEM.
        // LoadTile is atomic: an out-of-range rectangle must not leave a
        // partially copied tile behind or be reported as loaded.
        if (sourceStartS >= textureImageWidth_ ||
            rowTexels > textureImageWidth_ - sourceStartS ||
            destinationBase > tmem_.size() || destinationStride == 0)
            break;

        bool validTransfer = true;
        for (std::size_t row = 0; row < rowCount && validTransfer; ++row)
        {
            const std::size_t sourceRow = sourceStartT + row;
            if (sourceRow > (std::numeric_limits<std::uint64_t>::max() -
                             textureImageAddress_) / sourceRowBytes ||
                row > (std::numeric_limits<std::size_t>::max() - destinationBase) /
                          destinationStride)
            {
                validTransfer = false;
                break;
            }

            const std::uint64_t sourceRowAddress =
                static_cast<std::uint64_t>(textureImageAddress_) + sourceRow * sourceRowBytes;
            const std::size_t destinationRow = destinationBase + row * destinationStride;
            for (std::size_t column = 0; column < rowTexels; ++column)
            {
                const std::uint64_t sourceBit =
                    static_cast<std::uint64_t>(sourceStartS + column) * bitsPerTexel;
                const std::uint64_t sourceAddress = sourceRowAddress + sourceBit / 8U;
                const std::size_t destinationBit = column * bitsPerTexel;
                const std::size_t destinationAddress = destinationRow +
                    ((destinationBit / 8U) ^ ((row & 1U) != 0U ? 4U : 0U));
                const std::size_t sourceBytes = bitsPerTexel == 4U ? 1U : bitsPerTexel / 8U;
                const std::size_t destinationBytes = bitsPerTexel == 4U ? 1U : bitsPerTexel / 8U;
                if (sourceAddress > rdram.size() ||
                    sourceBytes > rdram.size() - sourceAddress ||
                    destinationAddress > tmem_.size() ||
                    destinationBytes > tmem_.size() - destinationAddress)
                {
                    validTransfer = false;
                    break;
                }
            }
        }
        if (!validTransfer)
            break;

        for (std::size_t row = 0; row < rowCount; ++row)
        {
            const std::size_t sourceRow = sourceStartT + row;
            const std::uint64_t sourceRowAddress =
                static_cast<std::uint64_t>(textureImageAddress_) + sourceRow * sourceRowBytes;
            const std::size_t destinationRow = destinationBase + row * destinationStride;

            for (std::size_t column = 0; column < rowTexels; ++column)
            {
                const std::size_t sourceTexel = sourceStartS + column;
                const std::uint64_t sourceBit = static_cast<std::uint64_t>(sourceTexel) * bitsPerTexel;
                const std::uint64_t sourceAddress = sourceRowAddress + sourceBit / 8U;
                const std::size_t destinationBit = column * bitsPerTexel;
                if (bitsPerTexel == 4U)
                {
                    const unsigned sourceShift = (sourceBit & 4U) ? 0U : 4U;
                    const unsigned destinationShift = (destinationBit & 4U) ? 0U : 4U;
                    const std::uint8_t nibble = static_cast<std::uint8_t>((rdram[sourceAddress] >> sourceShift) & 0x0FU);
                    const std::size_t destinationAddress = destinationRow +
                        ((destinationBit / 8U) ^ ((row & 1U) != 0U ? 4U : 0U));
                    tmem_[destinationAddress] = static_cast<std::uint8_t>(
                        (tmem_[destinationAddress] & ~(0x0FU << destinationShift)) |
                        (nibble << destinationShift));
                }
                else
                {
                    const std::size_t texelBytes = bitsPerTexel / 8U;
                    const std::size_t destinationAddress = destinationRow +
                        ((destinationBit / 8U) ^ ((row & 1U) != 0U ? 4U : 0U));
                    for (std::size_t byte = 0; byte < texelBytes; ++byte)
                        tmem_[destinationAddress + byte] = rdram[sourceAddress + byte];
                }
            }
        }
        tileState.loaded = true;
        break;
    }
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

    case 0x3C: // SetCombine
        if (lastCommand_.size() == 2U)
        {
            combineMux0_ = lastCommand_[0] & 0x00FFFFFFFFFFFFFFULL;
            combineMux1_ = lastCommand_[1];
        }
        break;

    case 0x24: // TextureRectangle
    case 0x25: // TextureRectangleFlip
    {
        if (lastCommand_.size() != 2 || colorImageFormat_ != 0 ||
            colorImageWidth_ == 0 || (colorImageSize_ != 2 && colorImageSize_ != 3))
            break;

        const std::uint64_t parameters = lastCommand_[1];
        const std::uint32_t xStart = static_cast<std::uint32_t>((command >> 44) & 0x0FFFU) >> 2;
        const std::uint32_t yStart = static_cast<std::uint32_t>((command >> 32) & 0x0FFFU) >> 2;
        const std::uint32_t xEnd = static_cast<std::uint32_t>((command >> 12) & 0x0FFFU) >> 2;
        const std::uint32_t yEnd = static_cast<std::uint32_t>(command & 0x0FFFU) >> 2;
        const std::size_t tileIndex = static_cast<std::size_t>((command >> 24) & 0x07U);
        const std::int32_t startS = static_cast<std::int16_t>(parameters >> 48);
        const std::int32_t startT = static_cast<std::int16_t>(parameters >> 32);
        const std::int32_t deltaS = static_cast<std::int16_t>(parameters >> 16);
        const std::int32_t deltaT = static_cast<std::int16_t>(parameters);
        const bool flipped = lastOpcode_ == 0x25;
        const std::size_t bytesPerPixel = colorImageSize_ == 2 ? 2U : 4U;
        const std::uint32_t left = std::min(std::max(xStart, scissorLeft_), colorImageWidth_);
        const std::uint32_t right = std::min({xEnd, scissorRight_, colorImageWidth_});
        const std::uint32_t top = std::max(yStart, scissorTop_);
        const std::uint32_t bottom = std::min(yEnd, scissorBottom_);

        for (std::uint32_t y = top; y < bottom; ++y)
        {
            for (std::uint32_t x = left; x < right; ++x)
            {
                const std::int64_t offsetX = static_cast<std::int64_t>(x - xStart);
                const std::int64_t offsetY = static_cast<std::int64_t>(y - yStart);
                const auto floorDiv32 = [](std::int64_t value) {
                    return value >= 0 ? value / 32 : -((-value + 31) / 32);
                };
                // S/T starts are 10.5 fixed point; dsdx/dtdy are 5.10.
                // Convert each per-pixel derivative to the coordinate's 5-bit fraction.
                const std::int64_t fixedS = static_cast<std::int64_t>(startS) +
                    floorDiv32((flipped ? offsetY : offsetX) * deltaS);
                const std::int64_t fixedT = static_cast<std::int64_t>(startT) +
                    floorDiv32((flipped ? offsetX : offsetY) * deltaT);
                const std::int64_t integerS = floorDiv32(fixedS);
                const std::int64_t integerT = floorDiv32(fixedT);
                const std::uint8_t fractionS = static_cast<std::uint8_t>(fixedS - integerS * 32);
                const std::uint8_t fractionT = static_cast<std::uint8_t>(fixedT - integerT * 32);
                const std::uint32_t color = textureRgbaFiltered(
                    tileIndex, integerS, integerT,
                    fractionS, fractionT);
                const std::uint64_t address = static_cast<std::uint64_t>(colorImageAddress_) +
                    (static_cast<std::uint64_t>(y) * colorImageWidth_ + x) * bytesPerPixel;
                if (address > rdram.size() || bytesPerPixel > rdram.size() - address)
                    continue;

                if (bytesPerPixel == 4U)
                {
                    rdram[address] = static_cast<std::uint8_t>(color >> 24);
                    rdram[address + 1U] = static_cast<std::uint8_t>(color >> 16);
                    rdram[address + 2U] = static_cast<std::uint8_t>(color >> 8);
                    rdram[address + 3U] = static_cast<std::uint8_t>(color);
                }
                else
                {
                    const std::uint16_t packed = static_cast<std::uint16_t>(
                        (((color >> 27) & 0x1FU) << 11) |
                        (((color >> 19) & 0x1FU) << 6) |
                        (((color >> 11) & 0x1FU) << 1) |
                        ((color & 0xFFU) >= 128U ? 1U : 0U));
                    rdram[address] = static_cast<std::uint8_t>(packed >> 8);
                    rdram[address + 1U] = static_cast<std::uint8_t>(packed);
                }
            }
        }
        break;
    }

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
                        const auto pixel = static_cast<std::uint16_t>(((static_cast<std::uint64_t>(y) * colorImageWidth_ + x) & 1U) ? fillColor_ : fillColor_ >> 16);
                        rdram[offset] = static_cast<std::uint8_t>(pixel >> 8);
                        rdram[offset + 1] = static_cast<std::uint8_t>(pixel);
                    }
                }
        }
        break;
    }

    case 0x08: // FillTriangle
    case 0x09: // ShadeTriangle
    case 0x0A: // TextureTriangle
    case 0x0B: // ShadeTextureTriangle
    case 0x0E: // TextureDepthTriangle
    case 0x0F: // ShadeTextureDepthTriangle
    {
        const bool textured = lastOpcode_ == 0x0A || lastOpcode_ == 0x0B ||
                              lastOpcode_ == 0x0E || lastOpcode_ == 0x0F;
        const bool shaded = lastOpcode_ == 0x09 || lastOpcode_ == 0x0B || lastOpcode_ == 0x0F;
        const std::size_t expectedWords = lastOpcode_ == 0x08 ? 4U :
            lastOpcode_ == 0x09 ? 12U : lastOpcode_ == 0x0A ? 12U : lastOpcode_ == 0x0B ? 20U :
            lastOpcode_ == 0x0E ? 14U : lastOpcode_ == 0x0F ? 22U : 0U;
        if (colorImageFormat_ != 0 || colorImageWidth_ == 0 ||
            (colorImageSize_ != 2 && colorImageSize_ != 3) ||
            lastCommand_.size() != expectedWords)
            break;

        const std::uint64_t w0 = lastCommand_[0];
        const std::uint64_t w1 = lastCommand_[1];
        const std::uint64_t w2 = lastCommand_[2];
        const std::uint64_t w3 = lastCommand_[3];
        const std::size_t textureTile = static_cast<std::size_t>((w0 >> 48) & 0x07U);
        const std::size_t textureOffset = 4U + (shaded ? 8U : 0U);
        const auto highSigned = [](std::uint64_t value) {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(value >> 32));
        };
        const auto lowSigned = [](std::uint64_t value) {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(value));
        };
        const std::int64_t startS = textured ? highSigned(lastCommand_[textureOffset]) : 0;
        const std::int64_t startT = textured ? lowSigned(lastCommand_[textureOffset]) : 0;
        // The eight-word texture block is S/T, W, dS/dX+dT/dX,
        // dW/dX, dS/dE+dT/dE, dW/dE, dS/dY+dT/dY, dW/dY.
        // Screen-space X and Y derivatives therefore live at offsets 2 and 6.
        const std::int64_t dsdx = textured ? highSigned(lastCommand_[textureOffset + 2U]) : 0;
        const std::int64_t dtdx = textured ? lowSigned(lastCommand_[textureOffset + 2U]) : 0;
        const std::int64_t dsdy = textured ? highSigned(lastCommand_[textureOffset + 6U]) : 0;
        const std::int64_t dtdy = textured ? lowSigned(lastCommand_[textureOffset + 6U]) : 0;
        // Triangle Y coordinates are packed low-to-high as YH, YM, YL.
        const int yh = static_cast<int>(w0 & 0x3FFFU);
        const int ym = static_cast<int>((w0 >> 16) & 0x3FFFU);
        const int yl = static_cast<int>((w0 >> 32) & 0x3FFFU);
        // Edge words are ordered XL/DXLDY, XH/DXHDY, XM/DXMDY.
        // XH is the long edge; the minor edge changes from XM to XL at YM.
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
                std::uint32_t color = fillColor_;
                if (textured)
                {
                    // Texture coefficients use signed 16.16 coordinates.
                    const std::int64_t relativeX = x - (xh >> 16);
                    const std::int64_t relativeY = y - (yh >> 2);
                    const std::int64_t fixedS = startS + relativeX * dsdx + relativeY * dsdy;
                    const std::int64_t fixedT = startT + relativeX * dtdx + relativeY * dtdy;
                    const auto floorDiv = [](std::int64_t value, std::int64_t divisor) {
                        const std::int64_t quotient = value / divisor;
            const std::int64_t remainder = value % divisor;
            return remainder < 0 ? quotient - 1 : quotient;
                    };
                    const std::int64_t integerS = floorDiv(fixedS, 1LL << 16);
                    const std::int64_t integerT = floorDiv(fixedT, 1LL << 16);
                    const std::uint8_t fractionS = static_cast<std::uint8_t>(
                        (fixedS - integerS * (1LL << 16)) >> 11);
                    const std::uint8_t fractionT = static_cast<std::uint8_t>(
                        (fixedT - integerT * (1LL << 16)) >> 11);
                    color = textureRgbaFiltered(textureTile, integerS, integerT, fractionS, fractionT);
                    const std::uint32_t textureColor = color;
                    std::uint32_t interpolatedShade = 0xFFFFFFFFU;
                    if (shaded)
                    {
                        // Shade coefficients are RG, BA pairs in 16.16 fixed point.
                        // The X and Y derivative blocks provide per-pixel interpolation.
                        const std::int64_t relativeShadeX = x - (xh >> 16);
                        const std::int64_t relativeShadeY = y - (yh >> 2);
                        std::uint32_t shade = 0;
                        for (std::size_t channel = 0; channel < 4; ++channel)
                        {
                            const std::size_t pair = channel / 2U;
                            const unsigned halfShift = (channel & 1U) == 0U ? 32U : 0U;
                            const std::int64_t initial = static_cast<std::int32_t>(
                                lastCommand_[4U + pair] >> halfShift);
                            const std::int64_t dx = static_cast<std::int32_t>(
                                lastCommand_[6U + pair] >> halfShift);
                            const std::int64_t dy = static_cast<std::int32_t>(
                                lastCommand_[10U + pair] >> halfShift);
                            const std::int64_t fixed = initial +
                                relativeShadeX * dx + relativeShadeY * dy;
                            const std::uint32_t component = static_cast<std::uint32_t>(
                                std::clamp<std::int64_t>(fixed >> 16, 0, 255));
                            shade |= component << (24U - static_cast<unsigned>(channel) * 8U);
                        }
                        interpolatedShade = shade;
                        const std::uint32_t red = (((color >> 24) & 0xFFU) * ((shade >> 24) & 0xFFU) + 127U) / 255U;
                        const std::uint32_t green = (((color >> 16) & 0xFFU) * ((shade >> 16) & 0xFFU) + 127U) / 255U;
                        const std::uint32_t blue = (((color >> 8) & 0xFFU) * ((shade >> 8) & 0xFFU) + 127U) / 255U;
                        const std::uint32_t alpha = ((color & 0xFFU) * (shade & 0xFFU) + 127U) / 255U;
                        color = (red << 24) | (green << 16) | (blue << 8) | alpha;
                    }

                    // A configured SetCombine mux evaluates the second cycle's
                    // color equation: (A - B) * C + D. With an untouched zero
                    // mux, retain the initial texture-pass behavior.
                    if (combineMux0_ != 0 || combineMux1_ != 0)
                    {
                        const std::uint32_t texture = textureColor;
                        const std::uint32_t primitive = primitiveColor_;
                        const std::uint32_t environment = environmentColor_;
                        const std::uint32_t shadeColor = interpolatedShade;
                        // First-cycle mux fields from the documented SetCombine layout.
                        const unsigned aSel = static_cast<unsigned>((combineMux0_ >> 52) & 0x0FU);
                        const unsigned cSel = static_cast<unsigned>((combineMux0_ >> 47) & 0x1FU);
                        const unsigned aaSel = static_cast<unsigned>((combineMux0_ >> 44) & 0x07U);
                        const unsigned acSel = static_cast<unsigned>((combineMux0_ >> 41) & 0x07U);
                        const unsigned bSel = static_cast<unsigned>((combineMux1_ >> 28) & 0x0FU);
                        const unsigned dSel = static_cast<unsigned>((combineMux1_ >> 15) & 0x07U);
                        const unsigned abSel = static_cast<unsigned>((combineMux1_ >> 12) & 0x07U);
                        const unsigned adSel = static_cast<unsigned>((combineMux1_ >> 9) & 0x07U);
                        const auto colorSource = [&](unsigned selector, unsigned channel) -> int {
                            switch (selector)
                            {
                            case 0: case 1: case 2:
                                return static_cast<int>((texture >> (24U - channel * 8U)) & 0xFFU);
                            case 3: return static_cast<int>((primitive >> (24U - channel * 8U)) & 0xFFU);
                            case 4: return static_cast<int>((shadeColor >> (24U - channel * 8U)) & 0xFFU);
                            case 5: return static_cast<int>((environment >> (24U - channel * 8U)) & 0xFFU);
                            case 6: return 255;
                            case 7: return 0;
                            default: return 0;
                            }
                        };
                        const auto alphaSource = [&](unsigned selector) -> int {
                            switch (selector)
                            {
                            case 0: case 1: case 2: return static_cast<int>(texture & 0xFFU);
                            case 3: return static_cast<int>(primitive & 0xFFU);
                            case 4: return static_cast<int>(shadeColor & 0xFFU);
                            case 5: return static_cast<int>(environment & 0xFFU);
                            case 6: return 255;
                            case 7: return 0;
                            default: return 0;
                            }
                        };
                        const auto alphaFactorSource = [&](unsigned selector) -> int {
                            switch (selector)
                            {
                            case 0: return primitiveLodFraction_;
                            case 1: case 2: return static_cast<int>(texture & 0xFFU);
                            case 3: return static_cast<int>(primitive & 0xFFU);
                            case 4: return static_cast<int>(shadeColor & 0xFFU);
                            case 5: return static_cast<int>(environment & 0xFFU);
                            case 6: return primitiveLodFraction_;
                            case 7: return 255;
                            default: return 0;
                            }
                        };
                        const auto colorFactor = [&](unsigned selector, unsigned channel) -> int {
                            switch (selector)
                            {
                            case 0: case 1: case 2: return colorSource(selector, channel);
                            case 6: return 0; // Key scale is not implemented yet.
                            case 7: return static_cast<int>(texture & 0xFFU); // Combined alpha.
                            case 8: case 9: return static_cast<int>(texture & 0xFFU);
                            case 10: return static_cast<int>(primitive & 0xFFU);
                            case 11: return static_cast<int>(shadeColor & 0xFFU);
                            case 12: return static_cast<int>(environment & 0xFFU);
                            case 13: case 14: return primitiveLodFraction_;
                            case 15: return 0; // K5 is supplied by SetConvert.
                            default: return colorSource(selector, channel);
                            }
                        };
                        std::uint32_t combined = 0;
                        for (unsigned channel = 0; channel < 3; ++channel)
                        {
                            const int a = colorSource(aSel, channel);
                            const int b = colorSource(bSel, channel);
                            const int factor = colorFactor(cSel, channel);
                            const int d = colorSource(dSel, channel);
                            const int value = std::clamp(((a - b) * factor + 127) / 255 + d, 0, 255);
                            combined |= static_cast<std::uint32_t>(value) << (24U - channel * 8U);
                        }
                        const int alphaA = alphaSource(aaSel);
                        const int alphaB = alphaSource(abSel);
                        const int alphaFactor = alphaFactorSource(acSel);
                        const int alphaD = alphaSource(adSel);
                        const int alpha = std::clamp(((alphaA - alphaB) * alphaFactor + 127) / 255 + alphaD, 0, 255);
                        color = combined | static_cast<std::uint32_t>(alpha);
                    }
                }
                else if (shaded)
                {
                    // ShadeTriangle uses the same RGBA coefficient block as
                    // ShadeTextureTriangle, but writes the interpolated shade
                    // directly instead of multiplying it by a texture sample.
                    const std::int64_t relativeShadeX = x - (xh >> 16);
                    const std::int64_t relativeShadeY = y - (yh >> 2);
                    color = 0;
                    for (std::size_t channel = 0; channel < 4; ++channel)
                    {
                        const std::size_t pair = channel / 2U;
                        const unsigned halfShift = (channel & 1U) == 0U ? 32U : 0U;
                        const std::int64_t initial = static_cast<std::int32_t>(
                            lastCommand_[4U + pair] >> halfShift);
                        const std::int64_t dx = static_cast<std::int32_t>(
                            lastCommand_[6U + pair] >> halfShift);
                        const std::int64_t dy = static_cast<std::int32_t>(
                            lastCommand_[10U + pair] >> halfShift);
                        const std::int64_t fixed = initial +
                            relativeShadeX * dx + relativeShadeY * dy;
                        const std::uint32_t component = static_cast<std::uint32_t>(
                            std::clamp<std::int64_t>(fixed >> 16, 0, 255));
                        color |= component << (24U - static_cast<unsigned>(channel) * 8U);
                    }
                }
                if (bpp == 4)
                {
                    rdram[offset] = static_cast<std::uint8_t>(color >> 24);
                    rdram[offset + 1] = static_cast<std::uint8_t>(color >> 16);
                    rdram[offset + 2] = static_cast<std::uint8_t>(color >> 8);
                    rdram[offset + 3] = static_cast<std::uint8_t>(color);
                }
                else
                {
                    const std::uint16_t pixel = (textured || shaded)
                        ? static_cast<std::uint16_t>((((color >> 27) & 0x1FU) << 11) |
                            (((color >> 19) & 0x3FU) << 5) | ((color >> 11) & 0x1FU))
                        : static_cast<std::uint16_t>(((static_cast<std::uint64_t>(y) * colorImageWidth_ + static_cast<unsigned>(x)) & 1U) ? fillColor_ : fillColor_ >> 16);
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

std::uint8_t Rdp::textureFilterMode() const
{
    return static_cast<std::uint8_t>((otherModesHigh_ >> 12) & 0x03U);
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

std::uint64_t Rdp::combineMux0() const
{
    return combineMux0_;
}

std::uint64_t Rdp::combineMux1() const
{
    return combineMux1_;
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

std::uint8_t Rdp::tmemByte(std::size_t index) const
{
    return index < tmem_.size() ? tmem_[index] : 0;
}

std::uint32_t Rdp::textureTexel(std::size_t tileIndex, std::int64_t s, std::int64_t t) const
{
    if (tileIndex >= tiles_.size())
        return 0;

    const RdpTileState &tileState = tiles_[tileIndex];
    if (!tileState.loaded || tileState.size > 3)
        return 0;

    const auto addressCoordinate = [](std::int64_t coordinate, std::uint8_t shift,
                                      std::uint8_t mask, std::uint8_t mirror,
                                      std::uint8_t clamp, std::size_t extent) {
        const auto floorDivide = [](std::int64_t value, std::int64_t divisor) {
            const std::int64_t quotient = value / divisor;
            const std::int64_t remainder = value % divisor;
            return remainder < 0 ? quotient - 1 : quotient;
        };
        if (shift <= 10U)
            coordinate = floorDivide(coordinate, std::int64_t(1) << shift);
        else
        {
            const unsigned leftShift = 16U - shift;
            if (coordinate > (std::numeric_limits<std::int64_t>::max() >> leftShift))
                coordinate = std::numeric_limits<std::int64_t>::max();
            else if (coordinate < (std::numeric_limits<std::int64_t>::min() >> leftShift))
                coordinate = std::numeric_limits<std::int64_t>::min();
            else
                coordinate *= std::int64_t(1) << leftShift;
        }

        if (clamp != 0U)
            return static_cast<std::int64_t>(std::clamp<std::int64_t>(
                coordinate, 0, extent == 0 ? 0 : static_cast<std::int64_t>(extent - 1U)));

        if (mask != 0U)
        {
            const std::int64_t period = std::int64_t(1) << mask;
            const std::int64_t block = floorDivide(coordinate, period);
            coordinate -= block * period;
            if (mirror != 0U && (block & 1LL) != 0)
                coordinate = period - 1 - coordinate;
        }
        return coordinate;
    };

    const std::size_t tileWidth = tileState.lowerRightS >= tileState.upperLeftS
        ? (static_cast<std::size_t>(tileState.lowerRightS - tileState.upperLeftS) >> 2) + 1U
        : 0U;
    const std::size_t tileHeight = tileState.lowerRightT >= tileState.upperLeftT
        ? (static_cast<std::size_t>(tileState.lowerRightT - tileState.upperLeftT) >> 2) + 1U
        : 0U;
    s = addressCoordinate(s, tileState.shiftS, tileState.maskS,
                           tileState.mirrorS, tileState.clampS, tileWidth);
    t = addressCoordinate(t, tileState.shiftT, tileState.maskT,
                           tileState.mirrorT, tileState.clampT, tileHeight);

    if (s < 0 || t < 0)
        return 0;

    const std::size_t bitsPerTexel = 4U << tileState.size;
    const std::size_t base = static_cast<std::size_t>(tileState.tmem) * 8U;
    std::size_t rowStride = static_cast<std::size_t>(tileState.line) * 8U;
    if (rowStride == 0)
    {
        const std::size_t width = tileState.lowerRightS >= tileState.upperLeftS
            ? (static_cast<std::size_t>(tileState.lowerRightS - tileState.upperLeftS) >> 2) + 1U
            : 1U;
        const std::size_t rowBytes = (width * bitsPerTexel + 7U) / 8U;
        rowStride = (rowBytes + 7U) & ~std::size_t(7U);
    }

    if (static_cast<std::uint64_t>(t) >
        static_cast<std::uint64_t>(tmem_.size() / rowStride))
        return 0;
    const std::size_t rowOffset = t * rowStride;
    if (base > tmem_.size() || rowOffset > tmem_.size() - base)
        return 0;

    if (static_cast<std::uint64_t>(s) >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() / bitsPerTexel))
        return 0;
    const std::size_t texelBit = s * bitsPerTexel;
    if (texelBit / 8U > tmem_.size() - base - rowOffset)
        return 0;
    const std::size_t byteOffset = base + rowOffset +
        ((texelBit / 8U) ^ ((t & 1U) != 0U ? 4U : 0U));
    const std::size_t bitInByte = texelBit & 7U;
    const std::size_t byteCount = bitsPerTexel / 8U;
    if (byteOffset >= tmem_.size())
        return 0;

    if (bitsPerTexel == 4U)
    {
        const unsigned shift = bitInByte == 0 ? 4U : 0U;
        return (tmem_[byteOffset] >> shift) & 0x0FU;
    }

    if (byteCount > tmem_.size() - byteOffset)
        return 0;

    // TMEM odd-row swapping exchanges the two 32-bit halves of each
    // 64-bit word. Apply the address transform to every byte, rather than
    // only the first byte, so texels that span a half-word boundary are
    // read from the same physical layout used by the load commands.
    std::uint32_t value = 0;
    for (std::size_t byte = 0; byte < byteCount; ++byte)
    {
        const std::size_t logicalByte = texelBit / 8U + byte;
        const std::size_t physicalOffset = base + rowOffset +
            (logicalByte ^ ((t & 1U) != 0U ? 4U : 0U));
        if (physicalOffset >= tmem_.size())
            return 0;
        value = (value << 8) | tmem_[physicalOffset];
    }
    return value;
}


std::uint32_t Rdp::textureRgba(std::size_t tileIndex, std::int64_t s, std::int64_t t) const
{
    if (tileIndex >= tiles_.size())
        return 0;

    const RdpTileState &tileState = tiles_[tileIndex];
    const std::uint32_t texel = textureTexel(tileIndex, s, t);
    const auto packIntensityAlpha = [](std::uint32_t intensity, std::uint32_t alpha) {
        return (intensity << 24) | (intensity << 16) | (intensity << 8) | alpha;
    };

    // YUV16 stores two luminance samples with shared U/V chroma:
    // [U, Y0] [V, Y1]. The conversion coefficients are signed s1.7.
    if (tileState.format == 1U)
    {
        if (tileState.size != 2U)
            return 0;

        const std::int64_t pairS = s & ~std::int64_t(1);
        const std::uint32_t first = textureTexel(tileIndex, pairS, t);
        const std::uint32_t second = textureTexel(tileIndex, pairS + 1U, t);
        const std::int32_t u = static_cast<std::int32_t>((first >> 8) & 0xFFU) - 128;
        const std::int32_t v = static_cast<std::int32_t>((second >> 8) & 0xFFU) - 128;
        const std::int32_t y = static_cast<std::int32_t>(
            (s & 1U) != 0 ? (second & 0xFFU) : (first & 0xFFU));

        const auto convertChannel = [y](std::int16_t firstCoefficient,
                                         std::int16_t secondCoefficient,
                                         std::int32_t firstChroma,
                                         std::int32_t secondChroma) {
            const std::int32_t value = y +
                ((static_cast<std::int32_t>(firstCoefficient) * firstChroma) >> 7) +
                ((static_cast<std::int32_t>(secondCoefficient) * secondChroma) >> 7);
            return static_cast<std::uint32_t>(std::clamp(value, 0, 255));
        };

        const std::uint32_t red = convertChannel(conversionCoefficients_[0], 0, v, 0);
        const std::uint32_t green = convertChannel(
            conversionCoefficients_[1], conversionCoefficients_[2], u, v);
        const std::uint32_t blue = convertChannel(conversionCoefficients_[3], 0, u, 0);
        return (red << 24) | (green << 16) | (blue << 8) | 0xFFU;
    }

    // RGBA: 16-bit is 5/5/5/1; 32-bit is already RGBA8888.
    if (tileState.format == 0U)
    {
        if (tileState.size == 3U)
            return texel;
        if (tileState.size != 2U)
            return 0;

        const std::uint32_t red = (texel >> 11) & 0x1FU;
        const std::uint32_t green = (texel >> 6) & 0x1FU;
        const std::uint32_t blue = (texel >> 1) & 0x1FU;
        const std::uint32_t alpha = texel & 1U;
        const auto expand5 = [](std::uint32_t channel) {
            return (channel << 3) | (channel >> 2);
        };
        return (expand5(red) << 24) | (expand5(green) << 16) |
               (expand5(blue) << 8) | (alpha ? 0xFFU : 0U);
    }

    // Color-indexed texels use the upper TMEM half as a replicated TLUT.
    if (tileState.format == 2U)
    {
        if ((otherModesHigh_ & (1U << 15)) == 0 ||
            (tileState.size != 0U && tileState.size != 1U))
            return 0;
        const std::size_t paletteIndex = tileState.size == 0U
            ? (static_cast<std::size_t>(tileState.palette) << 4) | (texel & 0x0FU)
            : (texel & 0xFFU);
        const std::size_t paletteAddress = 0x800U + paletteIndex * 8U;
        if (paletteAddress + 1U >= tmem_.size())
            return 0;
        const std::uint32_t entry =
            (static_cast<std::uint32_t>(tmem_[paletteAddress]) << 8) |
            tmem_[paletteAddress + 1U];
        if ((otherModesHigh_ & (1U << 14)) != 0)
        {
            const std::uint32_t intensity = (entry >> 8) & 0xFFU;
            return packIntensityAlpha(intensity, entry & 0xFFU);
        }
        const std::uint32_t red = (entry >> 11) & 0x1FU;
        const std::uint32_t green = (entry >> 6) & 0x1FU;
        const std::uint32_t blue = (entry >> 1) & 0x1FU;
        const auto expand5 = [](std::uint32_t channel) {
            return (channel << 3) | (channel >> 2);
        };
        return (expand5(red) << 24) | (expand5(green) << 16) |
               (expand5(blue) << 8) | ((entry & 1U) ? 0xFFU : 0U);
    }

    // IA: 4-bit uses I3A1, 8-bit uses I4A4, and 16-bit uses I8A8.
    if (tileState.format == 3U)
    {
        if (tileState.size == 0U)
        {
            const std::uint32_t intensity3 = (texel >> 1) & 0x07U;
            const std::uint32_t intensity8 =
                (intensity3 << 5) | (intensity3 << 2) | (intensity3 >> 1);
            return packIntensityAlpha(intensity8, (texel & 1U) ? 0xFFU : 0U);
        }
        if (tileState.size == 1U)
        {
            const std::uint32_t intensity4 = (texel >> 4) & 0x0FU;
            const std::uint32_t alpha4 = texel & 0x0FU;
            return packIntensityAlpha(intensity4 * 17U, alpha4 * 17U);
        }
        if (tileState.size == 2U)
            return packIntensityAlpha((texel >> 8) & 0xFFU, texel & 0xFFU);
        return 0;
    }

    // Intensity-only textures are opaque grayscale.
    if (tileState.format == 4U)
    {
        if (tileState.size == 0U)
            return packIntensityAlpha((texel & 0x0FU) * 17U, 0xFFU);
        if (tileState.size == 1U)
            return packIntensityAlpha(texel & 0xFFU, 0xFFU);
    }

    return 0;
}

std::uint32_t Rdp::textureRgbaBilinear(std::size_t tileIndex, std::int64_t s, std::int64_t t,
                                       std::uint8_t fractionS, std::uint8_t fractionT) const
{
    if (fractionS > 31U || fractionT > 31U)
        return 0;

    const std::uint32_t topLeft = textureRgba(tileIndex, s, t);
    const std::uint32_t topRight = textureRgba(tileIndex, s + 1U, t);
    const std::uint32_t bottomLeft = textureRgba(tileIndex, s, t + 1U);
    const std::uint32_t bottomRight = textureRgba(tileIndex, s + 1U, t + 1U);
    const std::uint32_t inverseS = 32U - fractionS;
    const std::uint32_t inverseT = 32U - fractionT;

    std::uint32_t result = 0;
    for (unsigned shift = 0; shift <= 24U; shift += 8U)
    {
        const std::uint32_t c00 = (topLeft >> shift) & 0xFFU;
        const std::uint32_t c10 = (topRight >> shift) & 0xFFU;
        const std::uint32_t c01 = (bottomLeft >> shift) & 0xFFU;
        const std::uint32_t c11 = (bottomRight >> shift) & 0xFFU;
        const std::uint32_t weighted =
            c00 * inverseS * inverseT + c10 * fractionS * inverseT +
            c01 * inverseS * fractionT + c11 * fractionS * fractionT;
        result |= ((weighted + 512U) >> 10U) << shift;
    }
    return result;
}


std::uint32_t Rdp::textureRgbaFiltered(std::size_t tileIndex, std::int64_t s, std::int64_t t,
                                       std::uint8_t fractionS, std::uint8_t fractionT) const
{
    if (fractionS > 31U || fractionT > 31U)
        return 0;

    const std::uint8_t filter = textureFilterMode();
    if (filter == 2U)
        return textureRgbaBilinear(tileIndex, s, t, fractionS, fractionT);

    if (filter == 3U)
    {
        const std::uint32_t samples[4] = {
            textureRgba(tileIndex, s, t),
            textureRgba(tileIndex, s + 1U, t),
            textureRgba(tileIndex, s, t + 1U),
            textureRgba(tileIndex, s + 1U, t + 1U)
        };
        std::uint32_t result = 0;
        for (unsigned shift = 0; shift <= 24U; shift += 8U)
        {
            const std::uint32_t sum =
                ((samples[0] >> shift) & 0xFFU) +
                ((samples[1] >> shift) & 0xFFU) +
                ((samples[2] >> shift) & 0xFFU) +
                ((samples[3] >> shift) & 0xFFU);
            result |= ((sum + 2U) >> 2U) << shift;
        }
        return result;
    }

    // Point filtering (and the reserved mode) selects the integer texel.
    return textureRgba(tileIndex, s, t);
}
