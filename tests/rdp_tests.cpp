#include "core/rdp/rdp.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void check(bool condition, const std::string &message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main()
{
    check(Rdp::commandWordCount(0x08) == 4, "fill triangle packet has four words");
    check(Rdp::commandWordCount(0x09) == 12, "shaded triangle packet has twelve words");
    check(Rdp::commandWordCount(0x0A) == 12, "textured triangle packet has twelve words");
    check(Rdp::commandWordCount(0x0B) == 20, "shaded textured triangle packet has twenty words");
    check(Rdp::commandWordCount(0x0C) == 6, "depth triangle packet has six words");
    check(Rdp::commandWordCount(0x08) + 8 == Rdp::commandWordCount(0x09),
          "shade coefficients add eight command words");
    check(Rdp::commandWordCount(0x08) + 8 == Rdp::commandWordCount(0x0A),
          "texture coefficients add eight command words");
    check(Rdp::commandWordCount(0x08) + 2 == Rdp::commandWordCount(0x0C),
          "depth coefficients add two command words");
    check(Rdp::commandWordCount(0x0D) == 14, "shaded depth triangle packet has fourteen words");
    check(Rdp::commandWordCount(0x0E) == 14, "textured depth triangle packet has fourteen words");
    check(Rdp::commandWordCount(0x0F) == 22, "full triangle packet has twenty-two words");
    check(Rdp::commandWordCount(0x24) == 2, "texture rectangle packet has two words");
    check(Rdp::commandWordCount(0x25) == 2, "flipped texture rectangle packet has two words");
    check(Rdp::commandWordCount(0x2F) == 1, "single-word command has one word");
    check(Rdp::commandWordCount(0x3F) == 1, "unknown command safely defaults to one word");

    Rdp rdp;
    check(!rdp.pushCommandWord(0x2400000000000000ULL),
          "texture rectangle waits for its parameter word");
    check(rdp.completedCommands() == 0, "incomplete command is not counted");
    check(rdp.pushCommandWord(0x123456789ABCDEF0ULL),
          "texture rectangle completes after two words");
    check(rdp.completedCommands() == 1, "completed command is counted");
    check(rdp.lastOpcode() == 0x24, "completed opcode is retained");
    check(rdp.lastWordCount() == 2, "completed packet length is retained");
    check(rdp.lastCommand().size() == 2 &&
          rdp.lastCommand()[1] == 0x123456789ABCDEF0ULL,
          "complete command words are retained");

    // SetOtherModes retains its 24-bit high and 32-bit low register fields.
    const std::uint32_t expectedModesHigh = 0x00ABCDEFU;
    const std::uint32_t expectedModesLow = 0x12345678U;
    check(rdp.pushCommandWord((0x2FULL << 56) |
          (static_cast<std::uint64_t>(expectedModesHigh) << 32) | expectedModesLow),
          "SetOtherModes completes as a single-word command");
    std::vector<std::uint8_t> commandBuffer;
    rdp.executeLastCommand(commandBuffer);
    check(rdp.otherModesHigh() == expectedModesHigh,
          "SetOtherModes stores its high register bits");
    check(rdp.otherModesLow() == expectedModesLow,
          "SetOtherModes stores its low register bits");
    check(rdp.cycleType() == ((expectedModesHigh >> 20) & 0x03U),
          "cycle type is decoded from the OtherModes high register");

    check(rdp.textureFilterMode() == ((expectedModesHigh >> 12) & 0x03U),
          "texture filter mode is decoded from OtherModes high bits");

    // OtherModes selects point, bilinear, and four-texel average sampling.
    Rdp filteredTexture;
    std::vector<std::uint8_t> filteredMemory(0x40, 0);
    filteredMemory[0x20] = 0x00; filteredMemory[0x21] = 0x00;
    filteredMemory[0x22] = 0x00; filteredMemory[0x23] = 0xFF;
    filteredMemory[0x24] = 0x00; filteredMemory[0x25] = 0x00;
    filteredMemory[0x26] = 0xFF; filteredMemory[0x27] = 0xFF;
    filteredMemory[0x28] = 0xFF; filteredMemory[0x29] = 0x00;
    filteredMemory[0x2A] = 0x00; filteredMemory[0x2B] = 0xFF;
    filteredMemory[0x2C] = 0x00; filteredMemory[0x2D] = 0xFF;
    filteredMemory[0x2E] = 0x00; filteredMemory[0x2F] = 0xFF;
    const auto submitFiltered = [&filteredTexture, &filteredMemory](std::uint64_t word) {
        if (filteredTexture.pushCommandWord(word))
            filteredTexture.executeLastCommand(filteredMemory);
    };
    submitFiltered((0x3DULL << 56) | (1ULL << 32) | (3ULL << 51) | 0x20ULL);
    submitFiltered((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitFiltered((0x32ULL << 56) | (4ULL << 12) | 4ULL);
    submitFiltered((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    check(filteredTexture.textureRgbaFiltered(0, 0, 0, 16, 16) == 0x000000FFU,
          "point texture filtering selects the integer-coordinate texel");
    submitFiltered((0x2FULL << 56) | (2ULL << 44));
    check(filteredTexture.textureFilterMode() == 2U,
          "SetOtherModes selects bilinear texture filtering");
    check(filteredTexture.textureRgbaFiltered(0, 0, 0, 16, 16) == 0x404040FFU,
          "bilinear OtherModes filtering interpolates four neighboring texels");
    submitFiltered((0x2FULL << 56) | (3ULL << 44));
    check(filteredTexture.textureFilterMode() == 3U,
          "SetOtherModes selects average texture filtering");
    check(filteredTexture.textureRgbaFiltered(0, 0, 0, 16, 16) == 0x404040FFU,
          "average texture filtering equally averages four neighboring texels");
    submitFiltered((0x2FULL << 56));
    check(filteredTexture.textureRgbaFiltered(0, 0, 0, 32, 0) == 0,
          "filtered texture sampling rejects invalid fractional coordinates");

    // RDP color commands retain their packed RGBA register values.
    const auto setColor = [&rdp, &commandBuffer](std::uint8_t opcode, std::uint32_t color) {
        if (rdp.pushCommandWord((static_cast<std::uint64_t>(opcode) << 56) | color))
            rdp.executeLastCommand(commandBuffer);
    };
    setColor(0x38, 0x10203040U);
    setColor(0x39, 0x50607080U);
    setColor(0x3A, 0x90A0B0C0U);
    setColor(0x3B, 0xD0E0F001U);
    check(rdp.pushCommandWord((0x3AULL << 56) | (0x35ULL << 40) |
                              (0x79ULL << 32) | 0x90A0B0C0ULL),
          "SetPrimitiveColor with LOD fields completes");
    rdp.executeLastCommand(commandBuffer);
    check(rdp.pushCommandWord((0x2EULL << 56) | (0xABCDULL << 16) | 0x1234ULL),
          "SetPrimDepth completes as a single-word command");
    rdp.executeLastCommand(commandBuffer);
    check(rdp.primitiveDepth() == 0xABCDU, "SetPrimDepth stores the depth value");
    check(rdp.primitiveDeltaZ() == 0x1234U, "SetPrimDepth stores the delta-Z value");

    // SetConvert packs six signed 9-bit coefficients from K0 through K5.
    const std::uint64_t convertCommand = (0x2CULL << 56) |
        (0x001ULL << 45) | (0x1FFULL << 36) | (0x100ULL << 27) |
        (0x0FFULL << 18) | (0x155ULL << 9) | 0x0AAULL;
    check(rdp.pushCommandWord(convertCommand), "SetConvert completes as a single-word command");
    rdp.executeLastCommand(commandBuffer);
    check(rdp.conversionCoefficient(0) == 1, "SetConvert decodes positive K0");
    check(rdp.conversionCoefficient(1) == -1, "SetConvert sign-extends negative K1");
    check(rdp.conversionCoefficient(2) == -256, "SetConvert sign-extends minimum K2");
    check(rdp.conversionCoefficient(3) == 255, "SetConvert decodes maximum positive K3");
    check(rdp.conversionCoefficient(4) == -171, "SetConvert sign-extends K4");
    check(rdp.conversionCoefficient(5) == 170, "SetConvert decodes K5");
    check(rdp.conversionCoefficient(6) == 0, "out-of-range conversion coefficient reads as zero");

    const std::uint64_t setTile = (0x35ULL << 56) | (5ULL << 53) | (2ULL << 51) |
        (0x12ULL << 41) | (0x34ULL << 32) | (3ULL << 24) | (9ULL << 20) |
        (3ULL << 18) | (0xAULL << 14) | (0x5ULL << 10) | (2ULL << 8) |
        (0xCULL << 4) | 0x7ULL;
    check(rdp.pushCommandWord(setTile), "SetTile completes as a single-word command");
    rdp.executeLastCommand(commandBuffer);
    const RdpTileState &decodedTile = rdp.tile(3);
    check(decodedTile.format == 5 && decodedTile.size == 2, "SetTile decodes format and texel size");
    check(decodedTile.line == 0x12 && decodedTile.tmem == 0x34, "SetTile decodes line and TMEM address");
    check(decodedTile.palette == 9, "SetTile decodes palette");
    check(decodedTile.clampT == 1 && decodedTile.mirrorT == 1 &&
          decodedTile.maskT == 0xA && decodedTile.shiftT == 5, "SetTile decodes T wrapping and shift fields");
    check(decodedTile.clampS == 1 && decodedTile.mirrorS == 0 &&
          decodedTile.maskS == 0xC && decodedTile.shiftS == 7, "SetTile decodes S wrapping and shift fields");

    const std::uint64_t setTileSize = (0x32ULL << 56) | (3ULL << 24) |
        (0x123ULL << 44) | (0x234ULL << 32) | (0x345ULL << 12) | 0x456ULL;
    check(rdp.pushCommandWord(setTileSize), "SetTileSize completes as a single-word command");
    rdp.executeLastCommand(commandBuffer);
    check(rdp.tile(3).upperLeftS == 0x123 && rdp.tile(3).upperLeftT == 0x234,
          "SetTileSize decodes upper-left S/T coordinates");
    check(rdp.tile(3).lowerRightS == 0x345 && rdp.tile(3).lowerRightT == 0x456,
          "SetTileSize decodes lower-right S/T coordinates");
    check(rdp.tile(0).upperLeftS == 0 && rdp.tile(0).lowerRightT == 0,
          "SetTileSize updates only the selected tile");
    check(rdp.tile(0).format == 0 && rdp.tile(8).format == 0,
          "SetTile leaves other tiles untouched and invalid reads return defaults");

    // LoadBlock and LoadTile retain the source rectangle and load mode per tile.
    const std::uint64_t loadBlock = (0x33ULL << 56) | (0x111ULL << 44) |
        (0x222ULL << 32) | (3ULL << 24) | (0x333ULL << 12) | 0x444ULL;
    check(rdp.pushCommandWord(loadBlock), "LoadBlock completes as a single-word command");
    rdp.executeLastCommand(commandBuffer);
    check(!rdp.tile(3).loaded && rdp.tile(3).loadType == 1,
          "LoadBlock records block mode but stays unloaded without a source image");
    check(rdp.tile(3).loadStartS == 0x111 && rdp.tile(3).loadStartT == 0x222 &&
          rdp.tile(3).loadEndS == 0x333 && rdp.tile(3).loadEndT == 0 &&
          rdp.tile(3).loadDxt == 0x444, "LoadBlock decodes coordinates and DXT");
    commandBuffer.resize(0x10000);
    check(rdp.pushCommandWord((0x3DULL << 56) | (1023ULL << 32)),
          "SetTextureImage configures a valid source image for LoadTile");
    rdp.executeLastCommand(commandBuffer);
    const std::uint64_t loadTile = (0x34ULL << 56) | (0x055ULL << 44) |
        (0x066ULL << 32) | (3ULL << 24) | (0x077ULL << 12) | 0x088ULL;
    check(rdp.pushCommandWord(loadTile), "LoadTile completes as a single-word command");
    rdp.executeLastCommand(commandBuffer);
    check(rdp.tile(3).loaded && rdp.tile(3).loadType == 2,
          "LoadTile marks the selected tile loaded with tile mode");
    check(rdp.tile(3).loadStartS == 0x055 && rdp.tile(3).loadStartT == 0x066 &&
          rdp.tile(3).loadEndS == 0x077 && rdp.tile(3).loadEndT == 0x088 &&
          rdp.tile(3).loadDxt == 0, "LoadTile decodes all four bounds");
    check(!rdp.tile(2).loaded, "texture loads do not modify other tiles");

    // LoadTile copies source texels from RDRAM into the selected TMEM tile.
    Rdp textureLoad;
    std::vector<std::uint8_t> textureRdram(0x30, 0);
    for (std::size_t i = 0; i < 8; ++i)
        textureRdram[0x10 + i] = static_cast<std::uint8_t>(0xA0U + i);
    const auto submitTexture = [&textureLoad, &textureRdram](std::uint64_t word) {
        if (textureLoad.pushCommandWord(word)) textureLoad.executeLastCommand(textureRdram);
    };
    submitTexture((0x3DULL << 56) | (1ULL << 51) | (3ULL << 32) | 0x10ULL);
    submitTexture((0x35ULL << 56) | (1ULL << 51) | (1ULL << 41) | (1ULL << 32));
    submitTexture((0x34ULL << 56) | (0ULL << 44) | (0ULL << 32) |
                  (0ULL << 24) | (12ULL << 12) | 4ULL);
    check(textureLoad.tile(0).loaded, "LoadTile completes a valid RDRAM-to-TMEM transfer");
    check(textureLoad.tmemByte(8) == 0xA0 && textureLoad.tmemByte(9) == 0xA1 &&
          textureLoad.tmemByte(10) == 0xA2 && textureLoad.tmemByte(11) == 0xA3,
          "LoadTile copies the first source row into the tile TMEM base");
    check(textureLoad.tmemByte(20) == 0xA4 && textureLoad.tmemByte(21) == 0xA5 &&
          textureLoad.tmemByte(22) == 0xA6 && textureLoad.tmemByte(23) == 0xA7,
          "LoadTile swaps the two 32-bit halves of each odd TMEM row");
    check(textureLoad.textureTexel(0, 0, 1) == 0xA4U &&
          textureLoad.textureTexel(0, 1, 1) == 0xA5U &&
          textureLoad.textureTexel(0, 2, 1) == 0xA6U &&
          textureLoad.textureTexel(0, 3, 1) == 0xA7U,
          "texture sampling reverses the odd-row TMEM word swap");
    check(textureLoad.tmemByte(7) == 0 && textureLoad.tmemByte(24) == 0,
          "LoadTile leaves bytes outside the requested tile rows untouched");

    // LoadBlock streams a linear run of source texels into the selected TMEM tile.
    Rdp blockLoad;
    std::vector<std::uint8_t> blockRdram(0x40, 0);
    blockRdram[0x20] = 0x12; blockRdram[0x21] = 0x34;
    blockRdram[0x22] = 0x56; blockRdram[0x23] = 0x78;
    blockRdram[0x24] = 0x9A; blockRdram[0x25] = 0xBC;
    blockRdram[0x26] = 0xDE; blockRdram[0x27] = 0xF0;
    const auto submitBlock = [&blockLoad, &blockRdram](std::uint64_t word) {
        if (blockLoad.pushCommandWord(word)) blockLoad.executeLastCommand(blockRdram);
    };
    submitBlock((0x3DULL << 56) | (2ULL << 51) | (7ULL << 32) | 0x20ULL);
    submitBlock((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) | (2ULL << 32));
    submitBlock((0x33ULL << 56) | (0ULL << 44) | (0ULL << 32) |
                (0ULL << 24) | (12ULL << 12));
    check(blockLoad.tile(0).loaded && blockLoad.tile(0).loadType == 1,
          "LoadBlock marks the tile loaded after copying source data");
    check(blockLoad.tmemByte(16) == 0x12 && blockLoad.tmemByte(17) == 0x34 &&
          blockLoad.tmemByte(18) == 0x56 && blockLoad.tmemByte(19) == 0x78 &&
          blockLoad.tmemByte(20) == 0x9A && blockLoad.tmemByte(21) == 0xBC &&
          blockLoad.tmemByte(22) == 0xDE && blockLoad.tmemByte(23) == 0xF0,
          "LoadBlock copies the requested 16-bit texels into sequential TMEM bytes");
    check(blockLoad.tmemByte(15) == 0 && blockLoad.tmemByte(24) == 0,
          "LoadBlock preserves TMEM bytes outside the transferred range");
    check(blockLoad.textureTexel(0, 0, 0) == 0x1234U &&
          blockLoad.textureTexel(0, 1, 0) == 0x5678U &&
          blockLoad.textureTexel(0, 2, 0) == 0x9ABCU &&
          blockLoad.textureTexel(0, 3, 0) == 0xDEF0U,
          "texture sampling reads big-endian 16-bit texels from the loaded tile");
    check(blockLoad.textureTexel(0, 0, 1) == 0 &&
          blockLoad.textureTexel(8, 0, 0) == 0,
          "texture sampling safely rejects unavailable rows and invalid tile indexes");

    // LoadBlock's DXT can wrap the TMEM destination row before the source
    // image row ends. Odd destination rows use the alternate 32-bit word half.
    Rdp oddBlockLoad;
    std::vector<std::uint8_t> oddBlockRdram(0x60, 0);
    for (std::size_t i = 0; i < 16; ++i)
    {
        oddBlockRdram[0x20 + i] = static_cast<std::uint8_t>(0x30U + i);
        oddBlockRdram[0x30 + i] = static_cast<std::uint8_t>(0x50U + i);
    }
    const auto submitOddBlock = [&oddBlockLoad, &oddBlockRdram](std::uint64_t word) {
        if (oddBlockLoad.pushCommandWord(word))
            oddBlockLoad.executeLastCommand(oddBlockRdram);
    };
    submitOddBlock((0x3DULL << 56) | (2ULL << 51) | (7ULL << 32) | 0x20ULL);
    submitOddBlock((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41));
    submitOddBlock((0x33ULL << 56) | (28ULL << 12) | 0x800ULL);
    check(oddBlockLoad.tmemByte(0) == 0x30 && oddBlockLoad.tmemByte(7) == 0x37 &&
          oddBlockLoad.tmemByte(12) == 0x38 && oddBlockLoad.tmemByte(13) == 0x39 &&
          oddBlockLoad.tmemByte(14) == 0x3A && oddBlockLoad.tmemByte(15) == 0x3B &&
          oddBlockLoad.tmemByte(8) == 0x3C && oddBlockLoad.tmemByte(9) == 0x3D &&
          oddBlockLoad.tmemByte(10) == 0x3E && oddBlockLoad.tmemByte(11) == 0x3F,
          "LoadBlock applies DXT row advancement and odd-row TMEM word swapping");
    check(oddBlockLoad.textureTexel(0, 0, 1) == 0x3839U &&
          oddBlockLoad.textureTexel(0, 1, 1) == 0x3A3BU,
          "texture sampling reads DXT-wrapped odd LoadBlock rows from the continuing source row");

    // 32-bit texels on odd rows must follow the same half-word swap
    // during both the load and the later texture read.
    Rdp oddRgba32Load;
    std::vector<std::uint8_t> oddRgba32Rdram(0x50, 0);
    const std::array<std::uint8_t, 16> rgba32Rows = {
        0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23,
        0x30, 0x31, 0x32, 0x33, 0x40, 0x41, 0x42, 0x43
    };
    for (std::size_t i = 0; i < rgba32Rows.size(); ++i)
        oddRgba32Rdram[0x20 + i] = rgba32Rows[i];
    const auto submitOddRgba32 = [&oddRgba32Load, &oddRgba32Rdram](std::uint64_t word) {
        if (oddRgba32Load.pushCommandWord(word))
            oddRgba32Load.executeLastCommand(oddRgba32Rdram);
    };
    submitOddRgba32((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitOddRgba32((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitOddRgba32((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    check(oddRgba32Load.textureTexel(0, 0, 0) == 0x10111213U &&
          oddRgba32Load.textureTexel(0, 1, 0) == 0x20212223U,
          "32-bit texture sampling reads the first LoadTile row");
    check(oddRgba32Load.textureTexel(0, 0, 1) == 0x30313233U &&
          oddRgba32Load.textureTexel(0, 1, 1) == 0x40414243U,
          "32-bit texture sampling reads both odd-row texels after TMEM half swapping");

    // Bilinear sampling interpolates RGBA channels with 5-bit fractions.
    Rdp bilinearTexture;
    std::vector<std::uint8_t> bilinearRdram(0x40, 0);
    const std::array<std::uint8_t, 16> bilinearPixels = {
        0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
        0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF
    };
    for (std::size_t i = 0; i < bilinearPixels.size(); ++i)
        bilinearRdram[0x20 + i] = bilinearPixels[i];
    const auto submitBilinear = [&bilinearTexture, &bilinearRdram](std::uint64_t word) {
        if (bilinearTexture.pushCommandWord(word))
            bilinearTexture.executeLastCommand(bilinearRdram);
    };
    submitBilinear((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitBilinear((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitBilinear((0x32ULL << 56) | (4ULL << 12) | 4ULL);
    submitBilinear((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    check(bilinearTexture.textureRgbaBilinear(0, 0, 0, 0, 0) == 0x000000FFU,
          "bilinear texture sampling returns the top-left texel at zero fractions");
    check(bilinearTexture.textureRgbaBilinear(0, 0, 0, 16, 16) == 0x404040FFU,
          "bilinear texture sampling averages four RGBA texels at half fractions");
    check(bilinearTexture.textureRgbaBilinear(0, 0, 0, 32, 0) == 0,
          "bilinear texture sampling rejects fractions outside the five-bit range");

    // A half-row DXT increment must carry after two 64-bit words, and
    // LoadBlock continues at the next source image row after eight 16-bit texels.
    Rdp fractionalDxtLoad;
    std::vector<std::uint8_t> fractionalDxtRdram(0x60, 0);
    for (std::size_t i = 0; i < 32; ++i)
        fractionalDxtRdram[0x20 + i] = static_cast<std::uint8_t>(0x40U + i);
    const auto submitFractionalDxt = [&fractionalDxtLoad, &fractionalDxtRdram](std::uint64_t word) {
        if (fractionalDxtLoad.pushCommandWord(word))
            fractionalDxtLoad.executeLastCommand(fractionalDxtRdram);
    };
    submitFractionalDxt((0x3DULL << 56) | (2ULL << 51) | (7ULL << 32) | 0x20ULL);
    submitFractionalDxt((0x35ULL << 56) | (2ULL << 51) | (2ULL << 41));
    submitFractionalDxt((0x33ULL << 56) | (60ULL << 12) | 0x400ULL);
    check(fractionalDxtLoad.tmemByte(0) == 0x40 &&
          fractionalDxtLoad.tmemByte(14) == 0x4E &&
          fractionalDxtLoad.tmemByte(20) == 0x50 &&
          fractionalDxtLoad.tmemByte(22) == 0x52 &&
          fractionalDxtLoad.tmemByte(16) == 0x54 &&
          fractionalDxtLoad.tmemByte(18) == 0x56,
          "LoadBlock accumulates fractional DXT per 64-bit word across source rows");

    // RGBA texture conversion expands 5-bit channels and preserves the RGBA16 alpha bit.
    Rdp rgba16;
    std::vector<std::uint8_t> rgba16Rdram(0x30, 0);
    rgba16Rdram[0x20] = 0xF8;
    rgba16Rdram[0x21] = 0x01; // RGBA16: full red, alpha set.
    const auto submitRgba16 = [&rgba16, &rgba16Rdram](std::uint64_t word) {
        if (rgba16.pushCommandWord(word)) rgba16.executeLastCommand(rgba16Rdram);
    };
    submitRgba16((0x3DULL << 56) | (2ULL << 51) | (7ULL << 32) | 0x20ULL);
    submitRgba16((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41));
    submitRgba16((0x33ULL << 56));
    check(rgba16.textureRgba(0, 0, 0) == 0xFF0000FFU,
          "RGBA16 texture conversion expands channels and maps the alpha bit to 8-bit alpha");

    // RGBA32 texture values are already in RGBA8888 channel order.
    Rdp rgba32;
    std::vector<std::uint8_t> rgba32Rdram(0x40, 0);
    rgba32Rdram[0x20] = 0x12;
    rgba32Rdram[0x21] = 0x34;
    rgba32Rdram[0x22] = 0x56;
    rgba32Rdram[0x23] = 0x78;
    const auto submitRgba32 = [&rgba32, &rgba32Rdram](std::uint64_t word) {
        if (rgba32.pushCommandWord(word)) rgba32.executeLastCommand(rgba32Rdram);
    };
    submitRgba32((0x3DULL << 56) | (3ULL << 51) | (7ULL << 32) | 0x20ULL);
    submitRgba32((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitRgba32((0x33ULL << 56));
    check(rgba32.textureRgba(0, 0, 0) == 0x12345678U,
          "RGBA32 texture conversion preserves all four source channels");
    check(rgba32.textureRgba(8, 0, 0) == 0,
          "RGBA texture conversion rejects invalid tile indexes");

    // YUV16 stores U/Y and V/Y pairs and uses the signed SetConvert coefficients.
    Rdp yuvTexture;
    std::vector<std::uint8_t> yuvMemory(0x40, 0);
    yuvMemory[0x20] = 160; // U shared by both pixels.
    yuvMemory[0x21] = 128; // Y0.
    yuvMemory[0x22] = 96;  // V shared by both pixels.
    yuvMemory[0x23] = 200; // Y1.
    const auto submitYuv = [&yuvTexture, &yuvMemory](std::uint64_t word) {
        if (yuvTexture.pushCommandWord(word))
            yuvTexture.executeLastCommand(yuvMemory);
    };
    submitYuv((0x3DULL << 56) | (1ULL << 32) | (2ULL << 51) | 0x20ULL);
    submitYuv((0x35ULL << 56) | (1ULL << 53) | (2ULL << 51) | (1ULL << 41));
    submitYuv((0x33ULL << 56) | (4ULL << 12)); // Load the two YUV texels.
    const std::uint64_t setYuvCoefficients = (0x2CULL << 56) |
        (175ULL << 45) |
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(-43) & 0x1FFU) << 36) |
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(-89) & 0x1FFU) << 27) |
        (222ULL << 18);
    submitYuv(setYuvCoefficients);
    check(yuvTexture.textureRgba(0, 0, 0) == 0x548BB7FFU,
          "YUV16 converts the first luminance sample using shared U/V chroma and signed coefficients");
    check(yuvTexture.textureRgba(0, 1, 0) == 0x9CD3FFFFU,
          "YUV16 converts the second luminance sample using the same chroma pair");

    // YUV conversion clamps channels instead of wrapping on under/overflow.
    Rdp clippedYuvTexture;
    std::vector<std::uint8_t> clippedYuvMemory(0x40, 0);
    clippedYuvMemory[0x20] = 0xFF;
    clippedYuvMemory[0x21] = 0xFF;
    clippedYuvMemory[0x22] = 0xFF;
    clippedYuvMemory[0x23] = 0x00;
    const auto submitClippedYuv = [&clippedYuvTexture, &clippedYuvMemory](std::uint64_t word) {
        if (clippedYuvTexture.pushCommandWord(word))
            clippedYuvTexture.executeLastCommand(clippedYuvMemory);
    };
    submitClippedYuv((0x3DULL << 56) | (2ULL << 51) | (7ULL << 32) | 0x20ULL);
    submitClippedYuv((0x35ULL << 56) | (1ULL << 53) | (2ULL << 51) | (1ULL << 41));
    submitClippedYuv((0x33ULL << 56) | (4ULL << 12));
    // Positive K0 drives red above 255; negative K3 drives blue below 0.
    const std::uint64_t extremeYuvCoefficients = (0x2CULL << 56) |
        (255ULL << 45) | (257ULL << 18);
    submitClippedYuv(extremeYuvCoefficients);
    const std::uint32_t overflowingYuv = clippedYuvTexture.textureRgba(0, 0, 0);
    const std::uint32_t underflowingYuv = clippedYuvTexture.textureRgba(0, 1, 0);
    check(((overflowingYuv >> 24) & 0xFFU) == 255U &&
          ((underflowingYuv >> 8) & 0xFFU) == 0U,
          "YUV16 conversion saturates overflowing and underflowing color channels");

    // IA and intensity texture formats expand to opaque grayscale/RGBA values.
    const auto sampleConvertedTexture = [](std::uint8_t imageSize, std::uint8_t format,
                                            std::uint8_t tileSize, std::uint8_t firstByte,
                                            std::uint8_t secondByte) {
        Rdp texture;
        std::vector<std::uint8_t> memory(0x40, 0);
        memory[0x20] = firstByte;
        memory[0x21] = secondByte;
        const auto submit = [&texture, &memory](std::uint64_t word) {
            if (texture.pushCommandWord(word))
                texture.executeLastCommand(memory);
        };
        submit((0x3DULL << 56) | (static_cast<std::uint64_t>(imageSize) << 51) |
               (7ULL << 32) | 0x20ULL);
        submit((0x35ULL << 56) | (static_cast<std::uint64_t>(format) << 53) |
               (static_cast<std::uint64_t>(tileSize) << 51) | (1ULL << 41));
        submit(0x33ULL << 56); // Load one texel.
        return texture.textureRgba(0, 0, 0);
    };
    check(sampleConvertedTexture(2, 3, 2, 0x80, 0x40) == 0x80808040U,
          "IA16 expands intensity and preserves 8-bit alpha");
    check(sampleConvertedTexture(1, 3, 1, 0xA5, 0) == 0xAAAAAA55U,
          "IA8 expands 4-bit intensity and alpha channels");
    check(sampleConvertedTexture(0, 3, 0, 0xD0, 0) == 0xDBDBDBFFU,
          "IA4 expands its 3-bit intensity and 1-bit alpha");
    check(sampleConvertedTexture(1, 4, 1, 0x7B, 0) == 0x7B7B7BFFU,
          "I8 expands intensity to opaque grayscale");
    check(sampleConvertedTexture(0, 4, 0, 0xC0, 0) == 0xCCCCCCFFU,
          "I4 expands intensity to opaque grayscale");
    check(sampleConvertedTexture(2, 4, 2, 0x7B, 0) == 0,
          "unsupported 16-bit intensity conversion returns zero");

    // LoadTLUT places replicated 16-bit entries in the upper TMEM half.
    Rdp ciTexture;
    std::vector<std::uint8_t> ciMemory(0x80, 0);
    ciMemory[0x20] = 0xF8;
    ciMemory[0x21] = 0x01; // Palette entry 0: opaque red RGBA5551.
    ciMemory[0x40] = 0x00;
    ciMemory[0x41] = 0x00;
    ciMemory[0x42] = 0x00;
    ciMemory[0x43] = 0x00;
    const auto submitCi = [&ciTexture, &ciMemory](std::uint64_t word) {
        if (ciTexture.pushCommandWord(word))
            ciTexture.executeLastCommand(ciMemory);
    };
    submitCi((0x3DULL << 56) | (2ULL << 51) | (7ULL << 32) | 0x20ULL);
    submitCi((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) | (0x100ULL << 32));
    submitCi((0x30ULL << 56)); // Load one palette entry at TMEM word 0x100.
    check(ciTexture.tile(0).loaded && ciTexture.tile(0).loadType == 3,
          "LoadTLUT marks the palette tile loaded");
    check(ciTexture.tmemByte(0x800) == 0xF8 && ciTexture.tmemByte(0x801) == 0x01 &&
          ciTexture.tmemByte(0x806) == 0xF8 && ciTexture.tmemByte(0x807) == 0x01,
          "LoadTLUT replicates each 16-bit palette entry across four TMEM banks");

    submitCi((0x3DULL << 56) | (1ULL << 51) | (7ULL << 32) | 0x40ULL);
    submitCi((0x35ULL << 56) | (2ULL << 53) | (1ULL << 51) | (1ULL << 41));
    submitCi((0x33ULL << 56)); // Load CI8 index zero into lower TMEM.
    submitCi((0x2FULL << 56) | (0x8000ULL << 32)); // Enable RGBA16 TLUT mode (high bit 15).
    check(ciTexture.textureRgba(0, 0, 0) == 0xFF0000FFU,
          "CI8 sampling resolves its index through the enabled RGBA16 TLUT");
    submitCi((0x2FULL << 56)); // Disable TLUT lookup.
    check(ciTexture.textureRgba(0, 0, 0) == 0,
          "CI texture conversion returns zero when TLUT lookup is disabled");

    // CI4 combines the tile palette bank with the texel's four-bit index.
    Rdp ci4Texture;
    std::vector<std::uint8_t> ci4Memory(0x80, 0);
    ci4Memory[0x20] = 0x00;
    ci4Memory[0x21] = 0x3F; // Palette entry 16: opaque blue RGBA5551.
    ci4Memory[0x40] = 0x00; // CI4 texel index zero.
    const auto submitCi4 = [&ci4Texture, &ci4Memory](std::uint64_t word) {
        if (ci4Texture.pushCommandWord(word))
            ci4Texture.executeLastCommand(ci4Memory);
    };
    submitCi4((0x3DULL << 56) | (2ULL << 51) | (7ULL << 32) | 0x20ULL);
    submitCi4((0x35ULL << 56) | (2ULL << 53) | (2ULL << 51) |
              (1ULL << 41) | (0x110ULL << 32));
    submitCi4((0x30ULL << 56)); // Load palette entry 16 at TMEM word 0x110.
    submitCi4((0x3DULL << 56) | (0ULL << 51) | (7ULL << 32) | 0x40ULL);
    submitCi4((0x35ULL << 56) | (2ULL << 53) | (1ULL << 41) |
              (1ULL << 32) | (1ULL << 20)); // CI4, palette bank 1.
    submitCi4((0x33ULL << 56));
    submitCi4((0x2FULL << 56) | (0x8000ULL << 32));
    check(ci4Texture.tile(0).palette == 1,
          "CI4 test tile retains palette bank one");
    check(ci4Texture.tmemByte(0x880) == 0x00 &&
          ci4Texture.tmemByte(0x881) == 0x3F,
          "CI4 palette bank one is loaded at its expected upper-TMEM address");
    check(ci4Texture.textureTexel(0, 0, 0) == 0,
          "CI4 texture source supplies palette index zero");
    check(ci4Texture.textureRgba(0, 0, 0) == 0x0000FFFFU,
          "CI4 sampling adds the tile palette bank before resolving its TLUT entry");

    // Failed TLUT transfers must be atomic and leave the tile unloaded.
    Rdp truncatedTlut;
    std::vector<std::uint8_t> shortPalette(0x21, 0);
    shortPalette[0x20] = 0xF8; // Only half of the 16-bit palette entry is present.
    const auto submitTruncated = [&truncatedTlut, &shortPalette](std::uint64_t word) {
        if (truncatedTlut.pushCommandWord(word))
            truncatedTlut.executeLastCommand(shortPalette);
    };
    submitTruncated((0x3DULL << 56) | (2ULL << 51) | (7ULL << 32) | 0x20ULL);
    submitTruncated((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                    (0x100ULL << 32));
    submitTruncated((0x30ULL << 56));
    check(!truncatedTlut.tile(0).loaded,
          "LoadTLUT rejects a palette entry truncated by the RDRAM boundary");
    check(truncatedTlut.tmemByte(0x800) == 0 && truncatedTlut.tmemByte(0x801) == 0,
          "failed LoadTLUT does not partially write a palette entry");

    Rdp overflowingTlut;
    std::vector<std::uint8_t> twoEntries(0x40, 0);
    const auto submitOverflowing = [&overflowingTlut, &twoEntries](std::uint64_t word) {
        if (overflowingTlut.pushCommandWord(word))
            overflowingTlut.executeLastCommand(twoEntries);
    };
    submitOverflowing((0x3DULL << 56) | (2ULL << 51) | (7ULL << 32) | 0x20ULL);
    submitOverflowing((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                      (0x1FFULL << 32));
    submitOverflowing((0x30ULL << 56) | (4ULL << 12)); // Two entries exceed the TMEM end.
    check(!overflowingTlut.tile(0).loaded,
          "LoadTLUT rejects a transfer that would exceed TMEM");
    check(overflowingTlut.tmemByte(0xFF8) == 0 &&
          overflowingTlut.tmemByte(0xFFF) == 0,
          "out-of-range LoadTLUT leaves the final TMEM bytes untouched");

    // IA16 TLUT mode interprets each palette entry as intensity followed by alpha.
    Rdp iaTlutTexture;
    std::vector<std::uint8_t> iaTlutMemory(0x80, 0);
    iaTlutMemory[0x20] = 0x80;
    iaTlutMemory[0x21] = 0x40;
    iaTlutMemory[0x40] = 0x00;
    const auto submitIaTlut = [&iaTlutTexture, &iaTlutMemory](std::uint64_t word) {
        if (iaTlutTexture.pushCommandWord(word))
            iaTlutTexture.executeLastCommand(iaTlutMemory);
    };
    submitIaTlut((0x3DULL << 56) | (2ULL << 51) | (7ULL << 32) | 0x20ULL);
    submitIaTlut((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                 (0x100ULL << 32));
    submitIaTlut((0x30ULL << 56));
    submitIaTlut((0x3DULL << 56) | (1ULL << 51) | (7ULL << 32) | 0x40ULL);
    submitIaTlut((0x35ULL << 56) | (2ULL << 53) | (1ULL << 51) |
                 (1ULL << 41));
    submitIaTlut((0x33ULL << 56));
    submitIaTlut((0x2FULL << 56) | (0xC000ULL << 32));
    check(iaTlutTexture.textureRgba(0, 0, 0) == 0x80808040U,
          "CI8 sampling converts IA16 TLUT entries to grayscale with the stored alpha");

    // Tile addressing applies masks, mirroring, clamping, and shifts before TMEM lookup.
    submitBlock((0x32ULL << 56) | (12ULL << 12));
    const auto submitTileAddressing = [&blockLoad, &blockRdram](std::uint64_t word) {
        if (blockLoad.pushCommandWord(word)) blockLoad.executeLastCommand(blockRdram);
    };
    submitTileAddressing((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                         (2ULL << 32) | (1ULL << 8) | (2ULL << 4));
    check(blockLoad.textureTexel(0, 4, 0) == 0xDEF0U &&
          blockLoad.textureTexel(0, 5, 0) == 0x9ABCU,
          "texture sampling wraps masked S coordinates and mirrors alternate periods");

    submitTileAddressing((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                         (2ULL << 32) | (2ULL << 8) | (2ULL << 4));
    check(blockLoad.textureTexel(0, 4, 0) == 0xDEF0U,
          "texture sampling clamps S coordinates to the final tile texel");

    submitTileAddressing((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                         (2ULL << 32) | 1ULL);
    check(blockLoad.textureTexel(0, 4, 0) == 0x9ABCU,
          "texture sampling applies right shifts to S coordinates");

    check(rdp.fogColor() == 0x10203040U, "SetFogColor stores packed RGBA");
    check(rdp.blendColor() == 0x50607080U, "SetBlendColor stores packed RGBA");
    check(rdp.primitiveColor() == 0x90A0B0C0U, "SetPrimitiveColor stores packed RGBA");
    check(rdp.primitiveMinLevel() == 0x35U, "SetPrimitiveColor decodes minimum LOD level");
    check(rdp.primitiveLodFraction() == 0x79U, "SetPrimitiveColor decodes primitive LOD fraction");
    check(rdp.environmentColor() == 0xD0E0F001U, "SetEnvironmentColor stores packed RGBA");

    // SetCombine is a two-word packet and preserves both mux payloads.
    Rdp combineRegisters;
    std::vector<std::uint8_t> combineMemory(16, 0);
    check(!combineRegisters.pushCommandWord((0x3CULL << 56) | 0x00123456789ABCDEULL),
          "SetCombine waits for its second 64-bit word");
    check(combineRegisters.pushCommandWord(0xFEDCBA9876543210ULL),
          "SetCombine completes after two 64-bit words");
    combineRegisters.executeLastCommand(combineMemory);
    check(combineRegisters.combineMux0() == 0x00123456789ABCDEULL,
          "SetCombine stores the first mux word");
    check(combineRegisters.combineMux1() == 0xFEDCBA9876543210ULL,
          "SetCombine stores the second mux word");
    combineRegisters.reset();
    check(combineRegisters.combineMux0() == 0 && combineRegisters.combineMux1() == 0,
          "reset clears SetCombine mux registers");

    rdp.reset();
    check(rdp.otherModesHigh() == 0 && rdp.otherModesLow() == 0,
          "reset clears OtherModes registers");
    check(rdp.cycleType() == 0, "reset restores one-cycle mode default");
    check(rdp.blendColor() == 0 && rdp.fogColor() == 0 &&
          rdp.primitiveColor() == 0 && rdp.environmentColor() == 0,
          "reset clears all RDP color registers");
    check(rdp.primitiveDepth() == 0 && rdp.primitiveDeltaZ() == 0,
          "reset clears primitive depth registers");
    check(rdp.conversionCoefficient(0) == 0 && rdp.conversionCoefficient(5) == 0,
          "reset clears conversion coefficients");
    check(rdp.tile(3).format == 0 && rdp.tile(3).line == 0 &&
          rdp.tile(3).upperLeftS == 0 && rdp.tile(3).lowerRightT == 0 && !rdp.tile(3).loaded,
          "reset clears tile descriptors and bounds");
    check(rdp.primitiveMinLevel() == 0 && rdp.primitiveLodFraction() == 0,
          "reset clears primitive color LOD parameters");
    check(rdp.completedCommands() == 0, "reset clears command count");
    check(rdp.lastWordCount() == 0, "reset clears previous command");

    check(!rdp.pushCommandWord(0x0800000000000000ULL),
          "triangle waits for remaining edge words");
    check(!rdp.pushCommandWord(1), "triangle remains incomplete after two words");
    check(!rdp.pushCommandWord(2), "triangle remains incomplete after three words");
    check(rdp.pushCommandWord(3), "triangle completes after four words");
    check(rdp.lastOpcode() == 0x08 && rdp.lastWordCount() == 4,
          "triangle packet framing is retained");

    // Exercise the first edge-only triangle path using a 32-bit color image.
    Rdp raster;
    std::vector<std::uint8_t> framebuffer(8 * 8 * 4, 0);
    const auto submit = [&raster, &framebuffer](std::uint64_t word) {
        if (raster.pushCommandWord(word))
            raster.executeLastCommand(framebuffer);
    };
    submit((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submit((0x37ULL << 56) | 0xFF0000FFULL);
    const std::uint64_t triangleHeader =
        (0x08ULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL;
    submit(triangleHeader);
    submit((5ULL << 16) << 32 | 65536U); // XL=5.0, DXLDY=+1
    submit((1ULL << 16) << 32 | 65536U); // XH=1.0, DXHDY=+1
    submit((5ULL << 16) << 32 | 3ULL * 65536U); // XM=5.0, DXMDY=+3
    check(framebuffer[(2 * 8 + 2) * 4] == 0xFF,
          "upper minor edge steps from XH and fills its span");
    check(framebuffer[(2 * 8 + 3) * 4] == 0xFF,
          "upper triangle fills the second pixel in its span");
    check(framebuffer[(4 * 8 + 4) * 4] == 0xFF,
          "lower minor edge steps from XM at the middle scanline");
    check(framebuffer[(2 * 8 + 7) * 4] == 0xFF,
          "upper minor edge advances from the middle-edge origin");

    // Repeat the same triangle into an RGBA16 image. Fill color alternates
    // its upper/lower halfword by pixel parity, as the RDP fill mode does.
    Rdp raster16;
    std::vector<std::uint8_t> framebuffer16(8 * 8 * 2, 0);
    const auto submit16 = [&raster16, &framebuffer16](std::uint64_t word) {
        if (raster16.pushCommandWord(word))
            raster16.executeLastCommand(framebuffer16);
    };
    submit16((0x3FULL << 56) | (2ULL << 51) | (7ULL << 32));
    submit16((0x37ULL << 56) | 0xF80007C1ULL);
    submit16(triangleHeader);
    submit16((5ULL << 16) << 32 | 65536U);
    submit16((1ULL << 16) << 32 | 65536U);
    submit16((5ULL << 16) << 32 | 3ULL * 65536U);
    const std::size_t evenPixel = (2 * 8 + 2) * 2;
    const std::size_t oddPixel = (2 * 8 + 3) * 2;
    check(framebuffer16[evenPixel] == 0xF8 &&
          framebuffer16[evenPixel + 1] == 0x00,
          "RGBA16 triangle writes the upper fill-color halfword to even pixels");
    check(framebuffer16[oddPixel] == 0x07 &&
          framebuffer16[oddPixel + 1] == 0xC1,
          "RGBA16 triangle writes the lower fill-color halfword to odd pixels");
    check(framebuffer16[(2 * 8 + 7) * 2] == 0x07 &&
          framebuffer16[(2 * 8 + 7) * 2 + 1] == 0xC1,
          "RGBA16 triangle follows the upper minor edge slope");

    // TextureRectangle loads RGBA32 texels through the selected tile and writes the framebuffer.
    Rdp textureRectangle;
    std::vector<std::uint8_t> textureRectangleMemory(0x200, 0);
    textureRectangleMemory[0x20] = 0xFF; textureRectangleMemory[0x21] = 0x00;
    textureRectangleMemory[0x22] = 0x00; textureRectangleMemory[0x23] = 0xFF;
    textureRectangleMemory[0x24] = 0x00; textureRectangleMemory[0x25] = 0xFF;
    textureRectangleMemory[0x26] = 0x00; textureRectangleMemory[0x27] = 0xFF;
    textureRectangleMemory[0x28] = 0x00; textureRectangleMemory[0x29] = 0x00;
    textureRectangleMemory[0x2A] = 0xFF; textureRectangleMemory[0x2B] = 0xFF;
    textureRectangleMemory[0x2C] = 0xFF; textureRectangleMemory[0x2D] = 0xFF;
    textureRectangleMemory[0x2E] = 0xFF; textureRectangleMemory[0x2F] = 0xFF;
    const auto submitTextureRectangle = [&textureRectangle, &textureRectangleMemory](std::uint64_t word) {
        if (textureRectangle.pushCommandWord(word))
            textureRectangle.executeLastCommand(textureRectangleMemory);
    };
    submitTextureRectangle((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitTextureRectangle((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitTextureRectangle((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitTextureRectangle((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    // 2x2 rectangle, S/T start at zero, one texel per pixel.
    submitTextureRectangle((0x24ULL << 56) | (8ULL << 12) | 8ULL);
    submitTextureRectangle((0ULL << 48) | (0ULL << 32) | (1024ULL << 16) | 1024ULL);
    check(textureRectangleMemory[0] == 0xFF && textureRectangleMemory[1] == 0x00 &&
          textureRectangleMemory[2] == 0x00 && textureRectangleMemory[3] == 0xFF,
          "TextureRectangle samples the top-left source texel into RGBA32 framebuffer");
    check(textureRectangleMemory[4] == 0x00 && textureRectangleMemory[5] == 0xFF &&
          textureRectangleMemory[6] == 0x00 && textureRectangleMemory[7] == 0xFF,
          "TextureRectangle advances S for the next horizontal pixel");
    check(textureRectangleMemory[8] == 0x00 && textureRectangleMemory[9] == 0x00 &&
          textureRectangleMemory[10] == 0xFF && textureRectangleMemory[11] == 0xFF,
          "TextureRectangle advances T for the next scanline");
    check(textureRectangleMemory[12] == 0xFF && textureRectangleMemory[13] == 0xFF &&
          textureRectangleMemory[14] == 0xFF && textureRectangleMemory[15] == 0xFF,
          "TextureRectangle samples the bottom-right source texel");

    // The flipped form swaps the horizontal and vertical texture derivatives.
    Rdp flippedTextureRectangle;
    std::vector<std::uint8_t> flippedMemory(0x200, 0);
    flippedMemory[0x20] = 0xFF; flippedMemory[0x21] = 0x00;
    flippedMemory[0x22] = 0x00; flippedMemory[0x23] = 0xFF;
    flippedMemory[0x24] = 0x00; flippedMemory[0x25] = 0xFF;
    flippedMemory[0x26] = 0x00; flippedMemory[0x27] = 0xFF;
    flippedMemory[0x28] = 0x00; flippedMemory[0x29] = 0x00;
    flippedMemory[0x2A] = 0xFF; flippedMemory[0x2B] = 0xFF;
    flippedMemory[0x2C] = 0xFF; flippedMemory[0x2D] = 0xFF;
    flippedMemory[0x2E] = 0xFF; flippedMemory[0x2F] = 0xFF;
    const auto submitFlipped = [&flippedTextureRectangle, &flippedMemory](std::uint64_t word) {
        if (flippedTextureRectangle.pushCommandWord(word))
            flippedTextureRectangle.executeLastCommand(flippedMemory);
    };
    submitFlipped((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitFlipped((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitFlipped((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitFlipped((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    submitFlipped((0x25ULL << 56) | (8ULL << 12) | 8ULL);
    submitFlipped((1024ULL << 16) | 1024ULL);
    check(flippedMemory[0] == 0xFF && flippedMemory[1] == 0x00 &&
          flippedMemory[2] == 0x00 && flippedMemory[3] == 0xFF,
          "TextureRectangleFlip keeps the top-left source texel");
    check(flippedMemory[4] == 0x00 && flippedMemory[5] == 0x00 &&
          flippedMemory[6] == 0xFF && flippedMemory[7] == 0xFF,
          "TextureRectangleFlip advances T across screen X");
    check(flippedMemory[8] == 0x00 && flippedMemory[9] == 0xFF &&
          flippedMemory[10] == 0x00 && flippedMemory[11] == 0xFF,
          "TextureRectangleFlip advances S across screen Y");
    check(flippedMemory[12] == 0xFF && flippedMemory[13] == 0xFF &&
          flippedMemory[14] == 0xFF && flippedMemory[15] == 0xFF,
          "TextureRectangleFlip samples the bottom-right source texel");

    // TextureTriangle uses its texture coefficient block to shade triangle spans.
    Rdp texturedTriangle;
    std::vector<std::uint8_t> texturedTriangleMemory(0x300, 0);
    const auto submitTexturedTriangle = [&texturedTriangle, &texturedTriangleMemory](std::uint64_t word) {
        if (texturedTriangle.pushCommandWord(word))
            texturedTriangle.executeLastCommand(texturedTriangleMemory);
    };
    texturedTriangleMemory[0x20] = 0xFF; texturedTriangleMemory[0x21] = 0x00;
    texturedTriangleMemory[0x22] = 0x00; texturedTriangleMemory[0x23] = 0xFF;
    texturedTriangleMemory[0x24] = 0x00; texturedTriangleMemory[0x25] = 0xFF;
    texturedTriangleMemory[0x26] = 0x00; texturedTriangleMemory[0x27] = 0xFF;
    texturedTriangleMemory[0x28] = 0x00; texturedTriangleMemory[0x29] = 0x00;
    texturedTriangleMemory[0x2A] = 0xFF; texturedTriangleMemory[0x2B] = 0xFF;
    texturedTriangleMemory[0x2C] = 0xFF; texturedTriangleMemory[0x2D] = 0xFF;
    texturedTriangleMemory[0x2E] = 0xFF; texturedTriangleMemory[0x2F] = 0xFF;
    submitTexturedTriangle((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitTexturedTriangle((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitTexturedTriangle((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitTexturedTriangle((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    submitTexturedTriangle((0x0AULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitTexturedTriangle((5ULL << 16) << 32 | 65536U);
    submitTexturedTriangle((1ULL << 16) << 32 | 65536U);
    submitTexturedTriangle((5ULL << 16) << 32 | 3ULL * 65536U);
    submitTexturedTriangle(0ULL); // S=0, T=0
    submitTexturedTriangle((65536ULL << 32)); // dS/dX = 1 texel
    submitTexturedTriangle(65536ULL); // dT/dY = 1 texel
    for (int i = 0; i < 5; ++i) submitTexturedTriangle(0ULL);
    check(texturedTriangleMemory[(2 * 8 + 2) * 4] == 0xFF &&
          texturedTriangleMemory[(2 * 8 + 2) * 4 + 1] == 0xFF &&
          texturedTriangleMemory[(2 * 8 + 2) * 4 + 2] == 0xFF,
          "TextureTriangle samples a texture texel into the triangle framebuffer");

    // A non-zero SetCombine mux selects the primitive color for textured pixels.
    submitTexturedTriangle((0x3AULL << 56) | (255ULL << 32) | 0x20A040FFULL);
    submitTexturedTriangle((0x3CULL << 56) | (3ULL << 37) | (6ULL << 32) |
                            (3ULL << 29) | (6ULL << 26));
    submitTexturedTriangle((3ULL << 6) | 3ULL);
    submitTexturedTriangle((0x0AULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitTexturedTriangle((5ULL << 16) << 32 | 65536U);
    submitTexturedTriangle((1ULL << 16) << 32 | 65536U);
    submitTexturedTriangle((5ULL << 16) << 32 | 3ULL * 65536U);
    submitTexturedTriangle(0ULL);
    submitTexturedTriangle((65536ULL << 32));
    submitTexturedTriangle(65536ULL);
    for (int i = 0; i < 5; ++i) submitTexturedTriangle(0ULL);
    check(texturedTriangleMemory[(2 * 8 + 2) * 4] == 0x20 &&
          texturedTriangleMemory[(2 * 8 + 2) * 4 + 1] == 0xA0 &&
          texturedTriangleMemory[(2 * 8 + 2) * 4 + 2] == 0x40 &&
          texturedTriangleMemory[(2 * 8 + 2) * 4 + 3] == 0xFF,
          "textured triangle color combiner selects primitive RGBA");

    // Shaded textured triangles place the texture coefficient block after eight shade words.
    Rdp shadedTexturedTriangle;
    std::vector<std::uint8_t> shadedTextureMemory(0x300, 0);
    const auto submitShadedTexture = [&shadedTexturedTriangle, &shadedTextureMemory](std::uint64_t word) {
        if (shadedTexturedTriangle.pushCommandWord(word))
            shadedTexturedTriangle.executeLastCommand(shadedTextureMemory);
    };
    shadedTextureMemory[0x20] = 0xFF; shadedTextureMemory[0x21] = 0x00;
    shadedTextureMemory[0x22] = 0x00; shadedTextureMemory[0x23] = 0xFF;
    shadedTextureMemory[0x24] = 0x00; shadedTextureMemory[0x25] = 0xFF;
    shadedTextureMemory[0x26] = 0x00; shadedTextureMemory[0x27] = 0xFF;
    shadedTextureMemory[0x28] = 0x00; shadedTextureMemory[0x29] = 0x00;
    shadedTextureMemory[0x2A] = 0xFF; shadedTextureMemory[0x2B] = 0xFF;
    shadedTextureMemory[0x2C] = 0xFF; shadedTextureMemory[0x2D] = 0xFF;
    shadedTextureMemory[0x2E] = 0xFF; shadedTextureMemory[0x2F] = 0xFF;
    submitShadedTexture((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitShadedTexture((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitShadedTexture((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitShadedTexture((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    submitShadedTexture((0x0BULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitShadedTexture((5ULL << 16) << 32 | 65536U);
    submitShadedTexture((1ULL << 16) << 32 | 65536U);
    submitShadedTexture((5ULL << 16) << 32 | 3ULL * 65536U);
    // Initial RGBA shade is (128, 64, 192, 255) in 16.16 fixed point.
    // Derivatives are zero, so every covered texel receives the same shade.
    submitShadedTexture((128ULL << 16) << 32 | (64ULL << 16));
    submitShadedTexture((192ULL << 16) << 32 | (255ULL << 16));
    for (int i = 0; i < 6; ++i) submitShadedTexture(0ULL);
    submitShadedTexture(0ULL); // S=0, T=0
    submitShadedTexture((65536ULL << 32)); // dS/dX = 1 texel
    submitShadedTexture(65536ULL); // dT/dY = 1 texel
    for (int i = 0; i < 5; ++i) submitShadedTexture(0ULL);
    check(shadedTextureMemory[(2 * 8 + 2) * 4] == 0x80 &&
          shadedTextureMemory[(2 * 8 + 2) * 4 + 1] == 0x40 &&
          shadedTextureMemory[(2 * 8 + 2) * 4 + 2] == 0xC0,
          "ShadeTextureTriangle modulates texture channels using interpolated RGBA shade");

    // Scissor clipping limits fill rectangles to the configured region.
    Rdp clipped;
    std::vector<std::uint8_t> clippedBuffer(8 * 8 * 4, 0);
    const auto submitClipped = [&clipped, &clippedBuffer](std::uint64_t word) {
        if (clipped.pushCommandWord(word))
            clipped.executeLastCommand(clippedBuffer);
    };
    submitClipped((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitClipped((0x37ULL << 56) | 0xAABBCCDDULL);
    submitClipped((0x2DULL << 56) | (8ULL << 44) | (4ULL << 32) |
                  (20ULL << 12) | 16ULL); // x=2..5, y=1..4
    submitClipped((0x36ULL << 56) | (28ULL << 12) | 28ULL); // full 7x7 rectangle
    check(clippedBuffer[(1 * 8 + 2) * 4] == 0xAA,
          "scissor permits pixels inside its top-left boundary");
    check(clippedBuffer[(3 * 8 + 4) * 4] == 0xAA,
          "scissor permits pixels inside its bottom-right boundary");
    check(clippedBuffer[(0 * 8 + 2) * 4] == 0,
          "scissor clips pixels above its top boundary");
    check(clippedBuffer[(1 * 8 + 1) * 4] == 0,
          "scissor clips pixels left of its boundary");
    check(clippedBuffer[(1 * 8 + 5) * 4] == 0,
          "scissor clips pixels at its exclusive right boundary");
    check(clippedBuffer[(4 * 8 + 2) * 4] == 0,
          "scissor clips pixels at its exclusive bottom boundary");

    // Triangle spans obey the same scissor rectangle as fill rectangles.
    Rdp clippedTriangle;
    std::vector<std::uint8_t> clippedTriangleBuffer(8 * 8 * 4, 0);
    const auto submitClippedTriangle = [&clippedTriangle, &clippedTriangleBuffer](std::uint64_t word) {
        if (clippedTriangle.pushCommandWord(word))
            clippedTriangle.executeLastCommand(clippedTriangleBuffer);
    };
    submitClippedTriangle((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitClippedTriangle((0x37ULL << 56) | 0x11223344ULL);
    submitClippedTriangle((0x2DULL << 56) | (12ULL << 44) | (8ULL << 32) |
                          (20ULL << 12) | 20ULL); // x=3..5, y=2..5
    submitClippedTriangle(triangleHeader);
    submitClippedTriangle((5ULL << 16) << 32 | 65536U);
    submitClippedTriangle((1ULL << 16) << 32 | 65536U);
    submitClippedTriangle((5ULL << 16) << 32 | 3ULL * 65536U);
    check(clippedTriangleBuffer[(2 * 8 + 2) * 4] == 0,
          "scissor clips triangle pixels left of its boundary");
    check(clippedTriangleBuffer[(4 * 8 + 4) * 4] == 0x11,
          "scissor preserves triangle pixels inside its boundary");

    // Reset restores the default full-frame scissor rectangle.
    // Reset also clears the color-image and fill-color registers, so restore
    // those before checking that the scissor state returned to its defaults.
    clipped.reset();
    submitClipped((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitClipped((0x37ULL << 56) | 0xAABBCCDDULL);
    submitClipped((0x36ULL << 56) | (28ULL << 12) | 28ULL);
    check(clippedBuffer[(6 * 8 + 6) * 4] == 0xAA,
          "reset restores full-frame scissor bounds");

    if (failures != 0)
    {
        std::cerr << failures << " RDP test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All RDP command-framing tests passed\n";
    return EXIT_SUCCESS;
}