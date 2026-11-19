#include "core/rdp/rdp.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
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

    // Exercise the streaming parser with every triangle packet size. Each
    // packet must complete on its final word, and the next opcode must begin
    // a fresh packet rather than being consumed as a coefficient.
    for (std::uint8_t opcode = 0x08; opcode <= 0x0F; ++opcode)
    {
        Rdp triangleParser;
        const std::size_t wordCount = Rdp::commandWordCount(opcode);
        for (std::size_t word = 0; word < wordCount; ++word)
        {
            const std::uint64_t packetWord = word == 0
                ? (static_cast<std::uint64_t>(opcode) << 56)
                : static_cast<std::uint64_t>(word);
            const bool completed = triangleParser.pushCommandWord(packetWord);
            check(completed == (word + 1U == wordCount),
                  "triangle parser completes only at the declared packet boundary");
        }
        check(triangleParser.completedCommands() == 1,
              "triangle parser counts one complete packet");
        check(triangleParser.lastOpcode() == opcode &&
              triangleParser.lastWordCount() == wordCount,
              "triangle parser retains the complete opcode and packet length");
        check(triangleParser.pushCommandWord(0x2F00000000000000ULL),
              "single-word command starts cleanly after a triangle packet");
        check(triangleParser.completedCommands() == 2,
              "command following a triangle packet is counted separately");
    }

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
    submitFiltered((0x2FULL << 56) | (1ULL << 44));
    check(filteredTexture.textureFilterMode() == 1U,
          "SetOtherModes selects three-point texture filtering");
    check(filteredTexture.textureRgbaFiltered(0, 0, 0, 8, 8) == 0x400040FFU,
          "three-point filtering interpolates the upper texture triangle");
    check(filteredTexture.textureRgbaFiltered(0, 0, 0, 24, 24) == 0x408040FFU,
          "three-point filtering interpolates the lower texture triangle");
    check(filteredTexture.textureRgbaFiltered(0, 0, 0, 16, 16) == 0x800080FFU,
          "three-point filtering handles samples on the diagonal");
    submitFiltered((0x2FULL << 56) | (2ULL << 44));
    check(filteredTexture.textureFilterMode() == 2U,
          "SetOtherModes selects bilinear texture filtering");
    check(filteredTexture.textureRgbaFiltered(0, 0, 0, 16, 16) == 0x404040FFU,
          "bilinear OtherModes filtering interpolates four neighboring texels");
    check(filteredTexture.textureRgbaBilinear(0, 0, 0, 32, 0) == 0,
          "bilinear texture sampling rejects out-of-range fractional S coordinates");
    check(filteredTexture.textureRgbaFiltered(0, 0, 0, 0, 32) == 0,
          "filtered texture sampling rejects out-of-range fractional T coordinates");
    check(filteredTexture.textureRgbaFiltered(8, 0, 0, 16, 16) == 0,
          "filtered texture sampling safely rejects an invalid tile index");
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

    // The RDP's 18-bit depth is stored as a 3-bit exponent and 11-bit mantissa.
    check(Rdp::compressDepth(0x00000U) == 0x0000U,
          "depth compression encodes the near-plane zero");
    check(Rdp::compressDepth(0x20000U) == 0x0800U,
          "depth compression advances to exponent one at its boundary");
    check(Rdp::compressDepth(0x3F800U) == 0x3800U,
          "depth compression encodes the far exponent boundary");
    check(Rdp::decompressDepth(0x0000U) == 0x00000U,
          "depth decompression restores zero");
    check(Rdp::decompressDepth(0x0800U) == 0x20000U,
          "depth decompression restores exponent-one boundary");
    check(Rdp::decompressDepth(0x3800U) == 0x3F800U,
          "depth decompression restores the far exponent boundary");
    std::uint32_t previousDecodedDepth = 0;
    bool depthCodecMonotonic = true;
    for (std::uint32_t depth = 0; depth <= 0x3FFFFU; depth += 257U)
    {
        const std::uint32_t decoded = Rdp::decompressDepth(Rdp::compressDepth(depth));
        if (decoded < previousDecodedDepth || decoded > depth)
            depthCodecMonotonic = false;
        previousDecodedDepth = decoded;
    }
    check(depthCodecMonotonic,
          "depth compression remains monotonic and never decodes beyond the source depth");

    // Primitive-Z comparisons reject a farther triangle and preserve the
    // nearer color/depth already stored at that pixel.
    Rdp depthTestRdp;
    std::vector<std::uint8_t> depthTestMemory(8 * 8 * 4 + 8 * 8 * 2, 0);
    const std::size_t depthBufferBase = 8 * 8 * 4;
    for (std::size_t i = depthBufferBase; i + 1 < depthTestMemory.size(); i += 2)
    {
        depthTestMemory[i] = 0xFF;
        depthTestMemory[i + 1] = 0xFC; // encoded far-plane depth
    }
    const auto submitDepthTest = [&depthTestRdp, &depthTestMemory](std::uint64_t word) {
        if (depthTestRdp.pushCommandWord(word))
            depthTestRdp.executeLastCommand(depthTestMemory);
    };
    submitDepthTest((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitDepthTest((0x3EULL << 56) | depthBufferBase);
    submitDepthTest((0x2FULL << 56) | 0x34U); // Z compare, Z update, primitive Z source
    submitDepthTest((0x2EULL << 56) | (0x1000ULL << 16) | 0x4000ULL);
    submitDepthTest((0x37ULL << 56) | 0xFF0000FFU);
    const auto drawDepthTestTriangle = [&submitDepthTest]() {
        submitDepthTest((0x08ULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
        submitDepthTest((5ULL << 16) << 32 | 65536U);
        submitDepthTest((1ULL << 16) << 32 | 65536U);
        submitDepthTest((5ULL << 16) << 32 | 3ULL * 65536U);
    };
    drawDepthTestTriangle();
    const std::size_t depthTestColorPixel = (2 * 8 + 2) * 4;
    const std::size_t depthTestZPixel = depthBufferBase + (2 * 8 + 2) * 2;
    check(depthTestMemory[depthTestColorPixel] == 0xFF,
          "primitive depth test writes a nearer triangle color");
    const std::uint16_t firstStoredDepth = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(depthTestMemory[depthTestZPixel]) << 8U) |
        depthTestMemory[depthTestZPixel + 1U]);
    check(firstStoredDepth != 0xFFFCU,
          "primitive depth update stores compressed depth in the Z image");
    submitDepthTest((0x2EULL << 56) | (0x2000ULL << 16) | 0x4000ULL);
    submitDepthTest((0x37ULL << 56) | 0x00FF00FFU);
    drawDepthTestTriangle();
    check(depthTestMemory[depthTestColorPixel] == 0xFF &&
          depthTestMemory[depthTestColorPixel + 1] == 0x00,
          "primitive depth comparison rejects a farther triangle");

    check(rdp.pushCommandWord((0x3EULL << 56) | 0x00123456ULL),
          "SetDepthImage completes as a single-word command");
    rdp.executeLastCommand(commandBuffer);
    check(rdp.depthImageAddress() == 0x00123456U,
          "SetDepthImage stores the 26-bit depth buffer address");

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

    // T-axis wrapping and mirroring must use the loaded tile height.
    Rdp mirroredTextureT;
    std::vector<std::uint8_t> mirroredTextureTMemory(0x40, 0);
    mirroredTextureTMemory[0x10] = 0x11; mirroredTextureTMemory[0x11] = 0x11;
    mirroredTextureTMemory[0x12] = 0x22; mirroredTextureTMemory[0x13] = 0x22;
    mirroredTextureTMemory[0x14] = 0x33; mirroredTextureTMemory[0x15] = 0x33;
    mirroredTextureTMemory[0x16] = 0x44; mirroredTextureTMemory[0x17] = 0x44;
    const auto submitMirroredT = [&mirroredTextureT, &mirroredTextureTMemory](std::uint64_t word) {
        if (mirroredTextureT.pushCommandWord(word))
            mirroredTextureT.executeLastCommand(mirroredTextureTMemory);
    };
    submitMirroredT((0x3DULL << 56) | (2ULL << 51) | (1ULL << 32) | 0x10ULL);
    submitMirroredT((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                    (1ULL << 14) | (1ULL << 18));
    submitMirroredT((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    check(mirroredTextureT.tile(0).loaded,
          "T-axis mirror fixture loads a two-row texture");
    check(mirroredTextureT.textureTexel(0, 0, 0) == 0x1111U &&
          mirroredTextureT.textureTexel(0, 0, 1) == 0x3333U,
          "texture sampling reads both rows before T-axis wrapping");
    check(mirroredTextureT.textureTexel(0, 0, 2) == 0x3333U &&
          mirroredTextureT.textureTexel(0, 0, 3) == 0x1111U,
          "texture sampling mirrors alternate masked T periods");

    // Set the tile's full two-row extent so clamping has the correct
    // final row rather than the default one-texel tile bounds.
    submitMirroredT((0x32ULL << 56) | 4ULL);
    // T clamping takes precedence over a configured mask and bounds both
    // negative and oversized coordinates to the loaded tile rectangle.
    submitMirroredT((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                    (2ULL << 18) | (1ULL << 14));
    check(mirroredTextureT.tile(0).clampT == 1,
          "SetTile enables T-axis clamping");
    check(mirroredTextureT.textureTexel(0, 0, -1) == 0x1111U &&
          mirroredTextureT.textureTexel(0, 0, 0) == 0x1111U,
          "T-axis clamping bounds negative coordinates to the first row");
    check(mirroredTextureT.textureTexel(0, 0, 2) == 0x3333U &&
          mirroredTextureT.textureTexel(0, 0, 3) == 0x3333U,
          "T-axis clamping bounds oversized coordinates to the final row");

    // T right-shift is applied before clamping to the tile's final row.
    submitMirroredT((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                    (2ULL << 18) | (1ULL << 14) | (1ULL << 10));
    check(mirroredTextureT.textureTexel(0, 0, 2) == 0x3333U &&
          mirroredTextureT.textureTexel(0, 0, 4) == 0x3333U,
          "T-axis right shift scales coordinates before clamping");

    // Shifts above ten move coordinates left before clamp evaluation.
    submitMirroredT((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                    (2ULL << 18) | (11ULL << 10));
    check(mirroredTextureT.textureTexel(0, 0, 1) == 0x3333U,
          "T-axis left shift scales coordinates before clamping");

    // Negative T coordinates use floor-based mask periods, not truncation
    // toward zero, when clamping is disabled.
    submitMirroredT((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                    (1ULL << 14));
    check(mirroredTextureT.textureTexel(0, 0, -1) == 0x3333U &&
          mirroredTextureT.textureTexel(0, 0, -2) == 0x1111U,
          "negative T coordinates wrap through masked tile periods");

    // A tile rectangle that extends beyond the source image must fail
    // without copying even the texels that would have fit.
    Rdp rejectedTileLoad;
    std::vector<std::uint8_t> rejectedTileMemory(0x20, 0);
    rejectedTileMemory[0x10] = 0xA5;
    const auto submitRejectedTile = [&rejectedTileLoad, &rejectedTileMemory](std::uint64_t word) {
        if (rejectedTileLoad.pushCommandWord(word))
            rejectedTileLoad.executeLastCommand(rejectedTileMemory);
    };
    submitRejectedTile((0x3DULL << 56) | (1ULL << 51) | 0x10ULL); // one-pixel source image
    submitRejectedTile((0x35ULL << 56) | (1ULL << 51) | (1ULL << 41));
    submitRejectedTile((0x34ULL << 56) | (4ULL << 12)); // requests two pixels
    check(!rejectedTileLoad.tile(0).loaded,
          "LoadTile rejects a rectangle wider than its source image");
    check(rejectedTileLoad.tmemByte(0) == 0,
          "rejected LoadTile leaves TMEM unchanged instead of partially copying");

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

    // LoadBlock must reject a truncated source run without retaining its
    // valid prefix in TMEM.
    Rdp rejectedBlockLoad;
    std::vector<std::uint8_t> rejectedBlockRdram(0x13, 0);
    rejectedBlockRdram[0x10] = 0x12;
    rejectedBlockRdram[0x11] = 0x34;
    const auto submitRejectedBlock = [&rejectedBlockLoad, &rejectedBlockRdram](std::uint64_t word) {
        if (rejectedBlockLoad.pushCommandWord(word))
            rejectedBlockLoad.executeLastCommand(rejectedBlockRdram);
    };
    submitRejectedBlock((0x3DULL << 56) | (2ULL << 51) | (1ULL << 32) | 0x10ULL);
    submitRejectedBlock((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41));
    submitRejectedBlock((0x33ULL << 56) | (4ULL << 12)); // two 16-bit texels
    check(!rejectedBlockLoad.tile(0).loaded,
          "LoadBlock rejects a source run truncated in the middle of a texel");
    check(rejectedBlockLoad.tmemByte(0) == 0 && rejectedBlockLoad.tmemByte(1) == 0,
          "rejected LoadBlock leaves TMEM unchanged instead of copying a valid prefix");

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

    // Signed texture coordinates must wrap through masked tiles instead of
    // being clamped to zero before the tile's address mode is applied.
    Rdp negativeTextureCoordinates;
    std::vector<std::uint8_t> negativeTextureMemory(0x30, 0);
    negativeTextureMemory[0x10] = 0x11; negativeTextureMemory[0x11] = 0x11;
    negativeTextureMemory[0x12] = 0x22; negativeTextureMemory[0x13] = 0x22;
    negativeTextureMemory[0x14] = 0x33; negativeTextureMemory[0x15] = 0x33;
    negativeTextureMemory[0x16] = 0x44; negativeTextureMemory[0x17] = 0x44;
    const auto submitNegativeTexture = [&negativeTextureCoordinates, &negativeTextureMemory](std::uint64_t word) {
        if (negativeTextureCoordinates.pushCommandWord(word))
            negativeTextureCoordinates.executeLastCommand(negativeTextureMemory);
    };
    submitNegativeTexture((0x3DULL << 56) | (2ULL << 51) | (3ULL << 32) | 0x10ULL);
    submitNegativeTexture((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) | (2ULL << 4));
    submitNegativeTexture((0x34ULL << 56) | (12ULL << 12));
    check(negativeTextureCoordinates.textureTexel(0, -1, 0) == 0x4444U,
          "negative S coordinates wrap to the final texel when tile masking is enabled");

    // S-axis mirroring reverses every alternate masked period, including
    // periods on the negative side of the coordinate origin.
    submitNegativeTexture((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                          (1ULL << 8) | (2ULL << 4));
    check(negativeTextureCoordinates.textureTexel(0, 4, 0) == 0x4444U &&
          negativeTextureCoordinates.textureTexel(0, 5, 0) == 0x3333U,
          "S-axis mirroring reverses the alternate positive mask period");
    check(negativeTextureCoordinates.textureTexel(0, -1, 0) == 0x1111U &&
          negativeTextureCoordinates.textureTexel(0, -2, 0) == 0x2222U,
          "S-axis mirroring reverses negative mask periods");

    // Define the complete four-texel tile width for S clamping.
    submitNegativeTexture((0x32ULL << 56) | (12ULL << 12));
    // S clamping takes precedence over the configured mask and mirror mode.
    submitNegativeTexture((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                          (2ULL << 8) | (2ULL << 4));
    check(negativeTextureCoordinates.textureTexel(0, -1, 0) == 0x1111U &&
          negativeTextureCoordinates.textureTexel(0, 4, 0) == 0x4444U,
          "S-axis clamping bounds coordinates before mask or mirror wrapping");

    // Right-shifting S coordinates scales them down before addressing.
    submitNegativeTexture((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                          (1ULL << 8) | (2ULL << 4) | 1ULL);
    check(negativeTextureCoordinates.textureTexel(0, 2, 0) == 0x2222U &&
          negativeTextureCoordinates.textureTexel(0, 4, 0) == 0x3333U,
          "S-axis right shift divides coordinates before texture addressing");

    // Shifts above ten move S coordinates left before clamp evaluation.
    submitNegativeTexture((0x35ULL << 56) | (2ULL << 51) | (1ULL << 41) |
                          (2ULL << 8) | (11ULL << 4));
    check(negativeTextureCoordinates.textureTexel(0, 1, 0) == 0x4444U,
          "S-axis left shift scales coordinates before clamping");

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

    // Triangle fill mode uses the same linear pixel parity when an
    // odd-width framebuffer makes successive scanlines start on opposite halves.
    Rdp oddWidthTriangle;
    std::vector<std::uint8_t> oddWidthTriangleMemory(3 * 8 * 2, 0);
    const auto submitOddWidthTriangle = [&oddWidthTriangle, &oddWidthTriangleMemory](std::uint64_t word) {
        if (oddWidthTriangle.pushCommandWord(word))
            oddWidthTriangle.executeLastCommand(oddWidthTriangleMemory);
    };
    submitOddWidthTriangle((0x3FULL << 56) | (2ULL << 51) | (2ULL << 32));
    submitOddWidthTriangle((0x37ULL << 56) | 0xF80007C1ULL);
    submitOddWidthTriangle(triangleHeader);
    submitOddWidthTriangle((5ULL << 16) << 32 | 65536U);
    submitOddWidthTriangle((1ULL << 16) << 32 | 65536U);
    submitOddWidthTriangle((5ULL << 16) << 32 | 3ULL * 65536U);
    const std::size_t oddWidthUpper = (1 * 3 + 2) * 2;
    const std::size_t oddWidthLower = (2 * 3 + 2) * 2;
    check(oddWidthTriangleMemory[oddWidthUpper] == 0x07 &&
          oddWidthTriangleMemory[oddWidthUpper + 1] == 0xC1,
          "RGBA16 triangle selects fill halfword using linear parity on odd-width rows");
    check(oddWidthTriangleMemory[oddWidthLower] == 0xF8 &&
          oddWidthTriangleMemory[oddWidthLower + 1] == 0x00,
          "RGBA16 triangle switches fill halfword at the next odd-width scanline");

    // RGBA16 fill-color halves alternate across the linear framebuffer,
    // including when an odd-width row changes the next row's starting half.
    Rdp oddWidthFill;
    std::vector<std::uint8_t> oddWidthFillMemory(3 * 2 * 2, 0);
    const auto submitOddWidthFill = [&oddWidthFill, &oddWidthFillMemory](std::uint64_t word) {
        if (oddWidthFill.pushCommandWord(word))
            oddWidthFill.executeLastCommand(oddWidthFillMemory);
    };
    submitOddWidthFill((0x3FULL << 56) | (2ULL << 51) | (2ULL << 32));
    submitOddWidthFill((0x37ULL << 56) | 0xF80007C1ULL);
    submitOddWidthFill((0x36ULL << 56) | (12ULL << 12) | 8ULL);
    check(oddWidthFillMemory[0] == 0xF8 && oddWidthFillMemory[1] == 0x00 &&
          oddWidthFillMemory[2] == 0x07 && oddWidthFillMemory[3] == 0xC1 &&
          oddWidthFillMemory[4] == 0xF8 && oddWidthFillMemory[5] == 0x00,
          "RGBA16 fill rectangle alternates fill halves across the first odd-width row");
    check(oddWidthFillMemory[6] == 0x07 && oddWidthFillMemory[7] == 0xC1 &&
          oddWidthFillMemory[8] == 0xF8 && oddWidthFillMemory[9] == 0x00 &&
          oddWidthFillMemory[10] == 0x07 && oddWidthFillMemory[11] == 0xC1,
          "RGBA16 fill rectangle continues fill-half alternation across odd-width rows");

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
    submitTextureRectangle((0x3FULL << 56) | (3ULL << 51) | (1ULL << 32));
    submitTextureRectangle((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitTextureRectangle((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitTextureRectangle((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    // LoadTile stores odd rows with the two 32-bit halves of each TMEM
    // 64-bit word exchanged. Verify the physical bytes before rasterization.
    check(textureRectangle.tmemByte(12) == 0x00 &&
          textureRectangle.tmemByte(13) == 0x00 &&
          textureRectangle.tmemByte(14) == 0xFF &&
          textureRectangle.tmemByte(15) == 0xFF,
          "LoadTile stores the first odd-row RGBA32 texel in the swapped half");
    check(textureRectangle.tmemByte(8) == 0xFF &&
          textureRectangle.tmemByte(9) == 0xFF &&
          textureRectangle.tmemByte(10) == 0xFF &&
          textureRectangle.tmemByte(11) == 0xFF,
          "LoadTile stores the second odd-row RGBA32 texel in the swapped half");
    check(textureRectangle.textureTexel(0, 0, 1) == 0x0000FFFFU,
          "RGBA32 texture sampling reads the first odd-row texel");
    check(textureRectangle.textureTexel(0, 1, 1) == 0xFFFFFFFFU,
          "RGBA32 texture sampling reads the second odd-row texel");
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
    submitFlipped((0x3FULL << 56) | (3ULL << 51) | (1ULL << 32));
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

    // Texture rectangles must apply the selected bilinear filter to fractional S/T.
    Rdp filteredTextureRectangle;
    std::vector<std::uint8_t> rectangleFilteredMemory(0x200, 0);
    rectangleFilteredMemory[0x20] = 0xFF; rectangleFilteredMemory[0x21] = 0x00;
    rectangleFilteredMemory[0x22] = 0x00; rectangleFilteredMemory[0x23] = 0xFF;
    rectangleFilteredMemory[0x24] = 0x00; rectangleFilteredMemory[0x25] = 0xFF;
    rectangleFilteredMemory[0x26] = 0x00; rectangleFilteredMemory[0x27] = 0xFF;
    rectangleFilteredMemory[0x28] = 0x00; rectangleFilteredMemory[0x29] = 0x00;
    rectangleFilteredMemory[0x2A] = 0xFF; rectangleFilteredMemory[0x2B] = 0xFF;
    rectangleFilteredMemory[0x2C] = 0xFF; rectangleFilteredMemory[0x2D] = 0xFF;
    rectangleFilteredMemory[0x2E] = 0xFF; rectangleFilteredMemory[0x2F] = 0xFF;
    const auto submitRectangleFiltered = [&filteredTextureRectangle, &rectangleFilteredMemory](std::uint64_t word) {
        if (filteredTextureRectangle.pushCommandWord(word))
            filteredTextureRectangle.executeLastCommand(rectangleFilteredMemory);
    };
    submitRectangleFiltered((0x3FULL << 56) | (3ULL << 51));
    submitRectangleFiltered((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitRectangleFiltered((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitRectangleFiltered((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    submitRectangleFiltered((0x2FULL << 56) | (2ULL << 44)); // Bilinear texture filtering.
    submitRectangleFiltered((0x24ULL << 56) | (4ULL << 12) | 4ULL); // One pixel at (0, 0).
    submitRectangleFiltered((16ULL << 48) | (16ULL << 32) | (512ULL << 16) | 512ULL);
    check(rectangleFilteredMemory[0] == 0x80 && rectangleFilteredMemory[1] == 0x80 &&
          rectangleFilteredMemory[2] == 0x80 && rectangleFilteredMemory[3] == 0xFF,
          "TextureRectangle bilinearly filters fractional S/T coordinates");

    // Texture rectangles must pack sampled RGBA into a 16-bit color image.
    Rdp textureRectangle16;
    std::vector<std::uint8_t> textureRectangle16Memory(0x200, 0);
    textureRectangle16Memory[0x20] = 0xFF; textureRectangle16Memory[0x21] = 0x00;
    textureRectangle16Memory[0x22] = 0x00; textureRectangle16Memory[0x23] = 0xFF;
    textureRectangle16Memory[0x24] = 0x00; textureRectangle16Memory[0x25] = 0xFF;
    textureRectangle16Memory[0x26] = 0x00; textureRectangle16Memory[0x27] = 0xFF;
    textureRectangle16Memory[0x28] = 0x00; textureRectangle16Memory[0x29] = 0x00;
    textureRectangle16Memory[0x2A] = 0xFF; textureRectangle16Memory[0x2B] = 0xFF;
    textureRectangle16Memory[0x2C] = 0xFF; textureRectangle16Memory[0x2D] = 0xFF;
    textureRectangle16Memory[0x2E] = 0xFF; textureRectangle16Memory[0x2F] = 0xFF;
    const auto submitTextureRectangle16 = [&textureRectangle16,
                                           &textureRectangle16Memory](std::uint64_t word) {
        if (textureRectangle16.pushCommandWord(word))
            textureRectangle16.executeLastCommand(textureRectangle16Memory);
    };
    submitTextureRectangle16((0x3FULL << 56) | (2ULL << 51) | (3ULL << 32));
    submitTextureRectangle16((0x3DULL << 56) | (3ULL << 51) |
                             (1ULL << 32) | 0x20ULL);
    submitTextureRectangle16((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitTextureRectangle16((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    submitTextureRectangle16((0x2FULL << 56) | (2ULL << 44));
    submitTextureRectangle16((0x24ULL << 56) | (4ULL << 12) | 4ULL);
    submitTextureRectangle16((16ULL << 48) | (16ULL << 32) |
                             (512ULL << 16) | 512ULL);
    check(textureRectangle16Memory[0] == 0x84 &&
          textureRectangle16Memory[1] == 0x21,
          "TextureRectangle packs bilinear RGBA output into a 16-bit color image");

    Rdp flippedTextureRectangle16;
    std::vector<std::uint8_t> flippedTextureRectangle16Memory(0x200, 0);
    flippedTextureRectangle16Memory[0x20] = 0xFF; flippedTextureRectangle16Memory[0x21] = 0x00;
    flippedTextureRectangle16Memory[0x22] = 0x00; flippedTextureRectangle16Memory[0x23] = 0xFF;
    flippedTextureRectangle16Memory[0x24] = 0x00; flippedTextureRectangle16Memory[0x25] = 0xFF;
    flippedTextureRectangle16Memory[0x26] = 0x00; flippedTextureRectangle16Memory[0x27] = 0xFF;
    flippedTextureRectangle16Memory[0x28] = 0x00; flippedTextureRectangle16Memory[0x29] = 0x00;
    flippedTextureRectangle16Memory[0x2A] = 0xFF; flippedTextureRectangle16Memory[0x2B] = 0xFF;
    flippedTextureRectangle16Memory[0x2C] = 0xFF; flippedTextureRectangle16Memory[0x2D] = 0xFF;
    flippedTextureRectangle16Memory[0x2E] = 0xFF; flippedTextureRectangle16Memory[0x2F] = 0xFF;
    const auto submitFlippedTextureRectangle16 = [&flippedTextureRectangle16,
        &flippedTextureRectangle16Memory](std::uint64_t word) {
        if (flippedTextureRectangle16.pushCommandWord(word))
            flippedTextureRectangle16.executeLastCommand(flippedTextureRectangle16Memory);
    };
    submitFlippedTextureRectangle16((0x3FULL << 56) | (2ULL << 51) | (3ULL << 32));
    submitFlippedTextureRectangle16((0x3DULL << 56) | (3ULL << 51) |
                                    (1ULL << 32) | 0x20ULL);
    submitFlippedTextureRectangle16((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitFlippedTextureRectangle16((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    submitFlippedTextureRectangle16((0x2FULL << 56) | (2ULL << 44));
    submitFlippedTextureRectangle16((0x25ULL << 56) | (4ULL << 12) | 4ULL);
    submitFlippedTextureRectangle16((16ULL << 48) | (16ULL << 32) |
                                    (512ULL << 16) | 512ULL);
    check(flippedTextureRectangle16Memory[0] == 0x84 &&
          flippedTextureRectangle16Memory[1] == 0x21,
          "TextureRectangleFlip packs bilinear RGBA output into a 16-bit color image");

    // The three-point filter must also be used by the rectangle rasterizer.
    Rdp threePointTextureRectangle;
    std::vector<std::uint8_t> threePointRectangleMemory(0x200, 0);
    threePointRectangleMemory[0x20] = 0xFF; threePointRectangleMemory[0x21] = 0x00;
    threePointRectangleMemory[0x22] = 0x00; threePointRectangleMemory[0x23] = 0xFF;
    threePointRectangleMemory[0x24] = 0x00; threePointRectangleMemory[0x25] = 0xFF;
    threePointRectangleMemory[0x26] = 0x00; threePointRectangleMemory[0x27] = 0xFF;
    threePointRectangleMemory[0x28] = 0x00; threePointRectangleMemory[0x29] = 0x00;
    threePointRectangleMemory[0x2A] = 0xFF; threePointRectangleMemory[0x2B] = 0xFF;
    threePointRectangleMemory[0x2C] = 0xFF; threePointRectangleMemory[0x2D] = 0xFF;
    threePointRectangleMemory[0x2E] = 0xFF; threePointRectangleMemory[0x2F] = 0xFF;
    const auto submitThreePointRectangle = [&threePointTextureRectangle,
                                             &threePointRectangleMemory](std::uint64_t word) {
        if (threePointTextureRectangle.pushCommandWord(word))
            threePointTextureRectangle.executeLastCommand(threePointRectangleMemory);
    };
    submitThreePointRectangle((0x3FULL << 56) | (3ULL << 51));
    submitThreePointRectangle((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitThreePointRectangle((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitThreePointRectangle((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    submitThreePointRectangle((0x2FULL << 56) | (1ULL << 44));
    submitThreePointRectangle((0x24ULL << 56) | (4ULL << 12) | 4ULL);
    submitThreePointRectangle((8ULL << 48) | (8ULL << 32) | (512ULL << 16) | 512ULL);
    check(threePointRectangleMemory[0] == 0x80 &&
          threePointRectangleMemory[1] == 0x40 &&
          threePointRectangleMemory[2] == 0x40 &&
          threePointRectangleMemory[3] == 0xFF,
          "TextureRectangle applies three-point filtering to fractional S/T coordinates");

    // TextureRectangleFlip must apply the same selected filter while swapping
    // the S/T derivative axes.
    Rdp flippedFilteredRectangle;
    std::vector<std::uint8_t> flippedFilteredMemory(0x200, 0);
    flippedFilteredMemory[0x20] = 0xFF; flippedFilteredMemory[0x21] = 0x00;
    flippedFilteredMemory[0x22] = 0x00; flippedFilteredMemory[0x23] = 0xFF;
    flippedFilteredMemory[0x24] = 0x00; flippedFilteredMemory[0x25] = 0xFF;
    flippedFilteredMemory[0x26] = 0x00; flippedFilteredMemory[0x27] = 0xFF;
    flippedFilteredMemory[0x28] = 0x00; flippedFilteredMemory[0x29] = 0x00;
    flippedFilteredMemory[0x2A] = 0xFF; flippedFilteredMemory[0x2B] = 0xFF;
    flippedFilteredMemory[0x2C] = 0xFF; flippedFilteredMemory[0x2D] = 0xFF;
    flippedFilteredMemory[0x2E] = 0xFF; flippedFilteredMemory[0x2F] = 0xFF;
    const auto submitFlippedFiltered = [&flippedFilteredRectangle,
                                        &flippedFilteredMemory](std::uint64_t word) {
        if (flippedFilteredRectangle.pushCommandWord(word))
            flippedFilteredRectangle.executeLastCommand(flippedFilteredMemory);
    };
    submitFlippedFiltered((0x3FULL << 56) | (3ULL << 51));
    submitFlippedFiltered((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitFlippedFiltered((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitFlippedFiltered((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    submitFlippedFiltered((0x2FULL << 56) | (2ULL << 44));
    submitFlippedFiltered((0x25ULL << 56) | (4ULL << 12) | (4ULL << 0));
    submitFlippedFiltered((16ULL << 48) | (16ULL << 32) | (512ULL << 16) | 512ULL);
    check(flippedFilteredMemory[0] == 0x80 &&
          flippedFilteredMemory[1] == 0x80 &&
          flippedFilteredMemory[2] == 0x80 &&
          flippedFilteredMemory[3] == 0xFF,
          "TextureRectangleFlip bilinearly filters fractional S/T coordinates");

    // Flipped rectangles must also honor the three-point filter mode.
    Rdp threePointFlippedRectangle;
    std::vector<std::uint8_t> threePointFlippedMemory(0x200, 0);
    threePointFlippedMemory[0x20] = 0xFF; threePointFlippedMemory[0x21] = 0x00;
    threePointFlippedMemory[0x22] = 0x00; threePointFlippedMemory[0x23] = 0xFF;
    threePointFlippedMemory[0x24] = 0x00; threePointFlippedMemory[0x25] = 0xFF;
    threePointFlippedMemory[0x26] = 0x00; threePointFlippedMemory[0x27] = 0xFF;
    threePointFlippedMemory[0x28] = 0x00; threePointFlippedMemory[0x29] = 0x00;
    threePointFlippedMemory[0x2A] = 0xFF; threePointFlippedMemory[0x2B] = 0xFF;
    threePointFlippedMemory[0x2C] = 0xFF; threePointFlippedMemory[0x2D] = 0xFF;
    threePointFlippedMemory[0x2E] = 0xFF; threePointFlippedMemory[0x2F] = 0xFF;
    const auto submitThreePointFlipped = [&threePointFlippedRectangle,
                                          &threePointFlippedMemory](std::uint64_t word) {
        if (threePointFlippedRectangle.pushCommandWord(word))
            threePointFlippedRectangle.executeLastCommand(threePointFlippedMemory);
    };
    submitThreePointFlipped((0x3FULL << 56) | (3ULL << 51));
    submitThreePointFlipped((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitThreePointFlipped((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitThreePointFlipped((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    submitThreePointFlipped((0x2FULL << 56) | (1ULL << 44));
    submitThreePointFlipped((0x25ULL << 56) | (4ULL << 12) | 4ULL);
    submitThreePointFlipped((8ULL << 48) | (8ULL << 32) | (512ULL << 16) | 512ULL);
    check(threePointFlippedMemory[0] == 0x80 &&
          threePointFlippedMemory[1] == 0x40 &&
          threePointFlippedMemory[2] == 0x40 &&
          threePointFlippedMemory[3] == 0xFF,
          "TextureRectangleFlip applies three-point filtering to fractional S/T coordinates");

    // The four-texel average mode is also honored by flipped rectangles.
    Rdp averagedFlippedRectangle;
    std::vector<std::uint8_t> averagedFlippedMemory(0x200, 0);
    averagedFlippedMemory[0x20] = 0xFF; averagedFlippedMemory[0x21] = 0x00;
    averagedFlippedMemory[0x22] = 0x00; averagedFlippedMemory[0x23] = 0xFF;
    averagedFlippedMemory[0x24] = 0x00; averagedFlippedMemory[0x25] = 0xFF;
    averagedFlippedMemory[0x26] = 0x00; averagedFlippedMemory[0x27] = 0xFF;
    averagedFlippedMemory[0x28] = 0x00; averagedFlippedMemory[0x29] = 0x00;
    averagedFlippedMemory[0x2A] = 0xFF; averagedFlippedMemory[0x2B] = 0xFF;
    averagedFlippedMemory[0x2C] = 0xFF; averagedFlippedMemory[0x2D] = 0xFF;
    averagedFlippedMemory[0x2E] = 0xFF; averagedFlippedMemory[0x2F] = 0xFF;
    const auto submitAveragedFlipped = [&averagedFlippedRectangle,
                                        &averagedFlippedMemory](std::uint64_t word) {
        if (averagedFlippedRectangle.pushCommandWord(word))
            averagedFlippedRectangle.executeLastCommand(averagedFlippedMemory);
    };
    submitAveragedFlipped((0x3FULL << 56) | (3ULL << 51));
    submitAveragedFlipped((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitAveragedFlipped((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitAveragedFlipped((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    submitAveragedFlipped((0x2FULL << 56) | (3ULL << 44));
    submitAveragedFlipped((0x25ULL << 56) | (4ULL << 12) | 4ULL);
    submitAveragedFlipped((16ULL << 48) | (16ULL << 32) | (512ULL << 16) | 512ULL);
    check(averagedFlippedMemory[0] == 0x80 &&
          averagedFlippedMemory[1] == 0x80 &&
          averagedFlippedMemory[2] == 0x80 &&
          averagedFlippedMemory[3] == 0xFF,
          "TextureRectangleFlip applies four-texel average filtering");

    // The non-flipped rectangle path must select the same four-texel average.
    Rdp averagedTextureRectangle;
    std::vector<std::uint8_t> averagedRectangleMemory(0x200, 0);
    averagedRectangleMemory[0x20] = 0xFF; averagedRectangleMemory[0x21] = 0x00;
    averagedRectangleMemory[0x22] = 0x00; averagedRectangleMemory[0x23] = 0xFF;
    averagedRectangleMemory[0x24] = 0x00; averagedRectangleMemory[0x25] = 0xFF;
    averagedRectangleMemory[0x26] = 0x00; averagedRectangleMemory[0x27] = 0xFF;
    averagedRectangleMemory[0x28] = 0x00; averagedRectangleMemory[0x29] = 0x00;
    averagedRectangleMemory[0x2A] = 0xFF; averagedRectangleMemory[0x2B] = 0xFF;
    averagedRectangleMemory[0x2C] = 0xFF; averagedRectangleMemory[0x2D] = 0xFF;
    averagedRectangleMemory[0x2E] = 0xFF; averagedRectangleMemory[0x2F] = 0xFF;
    const auto submitAveragedRectangle = [&averagedTextureRectangle,
                                          &averagedRectangleMemory](std::uint64_t word) {
        if (averagedTextureRectangle.pushCommandWord(word))
            averagedTextureRectangle.executeLastCommand(averagedRectangleMemory);
    };
    submitAveragedRectangle((0x3FULL << 56) | (3ULL << 51));
    submitAveragedRectangle((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitAveragedRectangle((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41));
    submitAveragedRectangle((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    submitAveragedRectangle((0x2FULL << 56) | (3ULL << 44));
    submitAveragedRectangle((0x24ULL << 56) | (4ULL << 12) | 4ULL);
    submitAveragedRectangle((16ULL << 48) | (16ULL << 32) | (512ULL << 16) | 512ULL);
    check(averagedRectangleMemory[0] == 0x80 &&
          averagedRectangleMemory[1] == 0x80 &&
          averagedRectangleMemory[2] == 0x80 &&
          averagedRectangleMemory[3] == 0xFF,
          "TextureRectangle applies four-texel average filtering");

    // Clipping a texture rectangle must preserve its original S/T origin.
    Rdp clippedTextureRectangle;
    std::vector<std::uint8_t> clippedTextureMemory(0x200, 0);
    clippedTextureMemory[0x20] = 0xFF; clippedTextureMemory[0x21] = 0x00;
    clippedTextureMemory[0x22] = 0x00; clippedTextureMemory[0x23] = 0xFF;
    clippedTextureMemory[0x24] = 0x00; clippedTextureMemory[0x25] = 0xFF;
    clippedTextureMemory[0x26] = 0x00; clippedTextureMemory[0x27] = 0xFF;
    clippedTextureMemory[0x28] = 0x00; clippedTextureMemory[0x29] = 0x00;
    clippedTextureMemory[0x2A] = 0xFF; clippedTextureMemory[0x2B] = 0xFF;
    clippedTextureMemory[0x2C] = 0xFF; clippedTextureMemory[0x2D] = 0xFF;
    clippedTextureMemory[0x2E] = 0xFF; clippedTextureMemory[0x2F] = 0xFF;
    const auto submitClippedTexture = [&clippedTextureRectangle, &clippedTextureMemory](std::uint64_t word) {
        if (clippedTextureRectangle.pushCommandWord(word))
            clippedTextureRectangle.executeLastCommand(clippedTextureMemory);
    };
    submitClippedTexture((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitClippedTexture((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitClippedTexture((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41) | (1ULL << 14));
    submitClippedTexture((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    submitClippedTexture((0x2DULL << 56) | (4ULL << 44) | (4ULL << 32) |
                         (8ULL << 12) | 12ULL); // x=1..2, y=1..3
    submitClippedTexture((0x24ULL << 56) | (4ULL << 44) |
                          (8ULL << 12) | 12ULL); // x=1..2, y=0..3
    submitClippedTexture((1024ULL << 16) | 1024ULL);
    check(clippedTextureMemory[0] == 0 && clippedTextureMemory[4] == 0,
          "scissor leaves clipped texture-rectangle pixels untouched");
    check(clippedTextureMemory[(1 * 8 + 1) * 4] == 0x00 &&
          clippedTextureMemory[(1 * 8 + 1) * 4 + 1] == 0x00 &&
          clippedTextureMemory[(1 * 8 + 1) * 4 + 2] == 0xFF,
          "scissored texture rectangle advances T from its unclipped origin");

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
    submitTexturedTriangle(0ULL); // W
    submitTexturedTriangle((65536ULL << 32)); // dS/dX = 1 texel
    submitTexturedTriangle(0ULL); // dW/dX
    submitTexturedTriangle(0ULL); // dS/dE, dT/dE
    submitTexturedTriangle(0ULL); // dW/dE
    submitTexturedTriangle(65536ULL); // dT/dY = 1 texel
    submitTexturedTriangle(0ULL); // dW/dY
    check(texturedTriangleMemory[(2 * 8 + 2) * 4] == 0xFF &&
          texturedTriangleMemory[(2 * 8 + 2) * 4 + 1] == 0xFF &&
          texturedTriangleMemory[(2 * 8 + 2) * 4 + 2] == 0xFF,
          "TextureTriangle samples a texture texel into the triangle framebuffer");

    // Exercise negative fixed-point S coordinates through the full triangle
    // rasterizer. A one-bit tile mask should wrap S=-1 to the second texel.
    Rdp negativeCoordinateTriangle;
    std::vector<std::uint8_t> negativeTriangleMemory(0x300, 0);
    const auto submitNegativeTriangle = [&negativeCoordinateTriangle, &negativeTriangleMemory](std::uint64_t word) {
        if (negativeCoordinateTriangle.pushCommandWord(word))
            negativeCoordinateTriangle.executeLastCommand(negativeTriangleMemory);
    };
    negativeTriangleMemory[0x20] = 0xFF; negativeTriangleMemory[0x21] = 0x00;
    negativeTriangleMemory[0x22] = 0x00; negativeTriangleMemory[0x23] = 0xFF;
    negativeTriangleMemory[0x24] = 0x00; negativeTriangleMemory[0x25] = 0xFF;
    negativeTriangleMemory[0x26] = 0x00; negativeTriangleMemory[0x27] = 0xFF;
    negativeTriangleMemory[0x28] = 0x00; negativeTriangleMemory[0x29] = 0x00;
    negativeTriangleMemory[0x2A] = 0xFF; negativeTriangleMemory[0x2B] = 0xFF;
    negativeTriangleMemory[0x2C] = 0xFF; negativeTriangleMemory[0x2D] = 0xFF;
    negativeTriangleMemory[0x2E] = 0xFF; negativeTriangleMemory[0x2F] = 0xFF;
    submitNegativeTriangle((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitNegativeTriangle((0x3DULL << 56) | (3ULL << 51) | (1ULL << 32) | 0x20ULL);
    submitNegativeTriangle((0x35ULL << 56) | (3ULL << 51) | (1ULL << 41) | (1ULL << 4));
    submitNegativeTriangle((0x34ULL << 56) | (4ULL << 12) | 4ULL);
    check(negativeCoordinateTriangle.textureTexel(0, -1, 0) == 0x00FF00FFU,
          "negative-S triangle setup loads the wrapped green texel");
    submitNegativeTriangle((0x0AULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitNegativeTriangle((5ULL << 16) << 32 | 65536U);
    submitNegativeTriangle((1ULL << 16) << 32 | 65536U);
    submitNegativeTriangle((5ULL << 16) << 32 | 3ULL * 65536U);
    submitNegativeTriangle(static_cast<std::uint64_t>(static_cast<std::uint32_t>(-131072)) << 32);
    // Texture coefficients: S/T, W, dS/dX+dT/dX, dW/dX,
    // dS/dE+dT/dE, dW/dE, dS/dY+dT/dY, dW/dY.
    submitNegativeTriangle(0ULL); // W
    submitNegativeTriangle(65536ULL << 32); // dS/dX = 1 texel
    for (int i = 0; i < 5; ++i) submitNegativeTriangle(0ULL);
    const std::size_t negativeTrianglePixel = (2 * 8 + 2) * 4;
    check(negativeTriangleMemory[negativeTrianglePixel] == 0x00 &&
          negativeTriangleMemory[negativeTrianglePixel + 1] == 0xFF &&
          negativeTriangleMemory[negativeTrianglePixel + 2] == 0x00,
          "TextureTriangle wraps negative S through the tile mask");

    // A non-zero SetCombine mux selects the primitive color for textured pixels.
    submitTexturedTriangle((0x3AULL << 56) | (255ULL << 32) | 0x20A040FFULL);
    submitTexturedTriangle((0x3CULL << 56) | (3ULL << 52) | (10ULL << 47) |
                            (3ULL << 44) | (7ULL << 41));
    submitTexturedTriangle((15ULL << 28) | (7ULL << 15) | (7ULL << 12) | (7ULL << 9));
    submitTexturedTriangle((0x0AULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitTexturedTriangle((5ULL << 16) << 32 | 65536U);
    submitTexturedTriangle((1ULL << 16) << 32 | 65536U);
    submitTexturedTriangle((5ULL << 16) << 32 | 3ULL * 65536U);
    submitTexturedTriangle(0ULL); // S=0, T=0
    submitTexturedTriangle(0ULL); // W
    submitTexturedTriangle((65536ULL << 32)); // dS/dX = 1 texel
    submitTexturedTriangle(0ULL); // dW/dX
    submitTexturedTriangle(0ULL); // dS/dE, dT/dE
    submitTexturedTriangle(0ULL); // dW/dE
    submitTexturedTriangle(65536ULL); // dT/dY = 1 texel
    submitTexturedTriangle(0ULL); // dW/dY
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
    submitShadedTexture(0ULL); // W
    submitShadedTexture((65536ULL << 32)); // dS/dX = 1 texel
    submitShadedTexture(0ULL); // dW/dX
    submitShadedTexture(0ULL); // dS/dE, dT/dE
    submitShadedTexture(0ULL); // dW/dE
    submitShadedTexture(65536ULL); // dT/dY = 1 texel
    submitShadedTexture(0ULL); // dW/dY
    check(shadedTextureMemory[(2 * 8 + 2) * 4] == 0x80 &&
          shadedTextureMemory[(2 * 8 + 2) * 4 + 1] == 0x40 &&
          shadedTextureMemory[(2 * 8 + 2) * 4 + 2] == 0xC0,
          "ShadeTextureTriangle modulates texture channels using interpolated RGBA shade");

    // ShadeTriangle (opcode 0x09) writes interpolated RGBA directly,
    // without requiring a texture image or texture coefficient block.
    Rdp shadedTriangle;
    std::vector<std::uint8_t> shadedTriangleMemory(8 * 8 * 4, 0);
    const auto submitShadedTriangle = [&shadedTriangle, &shadedTriangleMemory](std::uint64_t word) {
        if (shadedTriangle.pushCommandWord(word))
            shadedTriangle.executeLastCommand(shadedTriangleMemory);
    };
    submitShadedTriangle((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitShadedTriangle((0x09ULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitShadedTriangle((5ULL << 16) << 32 | 65536U);
    submitShadedTriangle((1ULL << 16) << 32 | 65536U);
    submitShadedTriangle((5ULL << 16) << 32 | 3ULL * 65536U);
    submitShadedTriangle((128ULL << 16) << 32 | (64ULL << 16));
    submitShadedTriangle((192ULL << 16) << 32 | (255ULL << 16));
    for (int i = 0; i < 6; ++i) submitShadedTriangle(0ULL);
    const std::size_t shadedTrianglePixel = (2 * 8 + 2) * 4;
    check(shadedTriangleMemory[shadedTrianglePixel] == 0x80 &&
          shadedTriangleMemory[shadedTrianglePixel + 1] == 0x40 &&
          shadedTriangleMemory[shadedTrianglePixel + 2] == 0xC0 &&
          shadedTriangleMemory[shadedTrianglePixel + 3] == 0xFF,
          "ShadeTriangle writes interpolated RGBA without texture sampling");

    // The same shaded triangle must be converted to RGB5551/RGB565-style
    // packed output when the color image is 16 bits per pixel.
    Rdp shadedTriangle16;
    std::vector<std::uint8_t> shadedTriangle16Memory(8 * 8 * 2, 0);
    const auto submitShadedTriangle16 = [&shadedTriangle16, &shadedTriangle16Memory](std::uint64_t word) {
        if (shadedTriangle16.pushCommandWord(word))
            shadedTriangle16.executeLastCommand(shadedTriangle16Memory);
    };
    submitShadedTriangle16((0x3FULL << 56) | (2ULL << 51) | (7ULL << 32));
    submitShadedTriangle16((0x09ULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitShadedTriangle16((5ULL << 16) << 32 | 65536U);
    submitShadedTriangle16((1ULL << 16) << 32 | 65536U);
    submitShadedTriangle16((5ULL << 16) << 32 | 3ULL * 65536U);
    submitShadedTriangle16((128ULL << 16) << 32 | (64ULL << 16));
    submitShadedTriangle16((192ULL << 16) << 32 | (255ULL << 16));
    for (int i = 0; i < 6; ++i) submitShadedTriangle16(0ULL);
    const std::size_t shadedTriangle16Pixel = (2 * 8 + 2) * 2;
    check(shadedTriangle16Memory[shadedTriangle16Pixel] == 0x82 &&
          shadedTriangle16Memory[shadedTriangle16Pixel + 1] == 0x18,
          "ShadeTriangle packs interpolated color into a 16-bit color image");

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

    // FillDepthTriangle consumes the two Z coefficient words and writes
    // interpolated pixel depth when primitive-depth source selection is off.
    Rdp interpolatedDepthTriangle;
    std::vector<std::uint8_t> interpolatedDepthMemory(8 * 8 * 4 + 8 * 8 * 2, 0);
    for (std::size_t i = 256; i < interpolatedDepthMemory.size(); i += 2)
    {
        interpolatedDepthMemory[i] = 0xFF;
        interpolatedDepthMemory[i + 1] = 0xFC;
    }
    const auto submitInterpolatedDepth = [&interpolatedDepthTriangle, &interpolatedDepthMemory](std::uint64_t word) {
        if (interpolatedDepthTriangle.pushCommandWord(word))
            interpolatedDepthTriangle.executeLastCommand(interpolatedDepthMemory);
    };
    submitInterpolatedDepth((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitInterpolatedDepth((0x3EULL << 56) | 256ULL);
    submitInterpolatedDepth((0x2FULL << 56) | 0x30ULL); // Z compare/update, pixel Z source
    submitInterpolatedDepth((0x0CULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitInterpolatedDepth((5ULL << 16) << 32 | 65536U);
    submitInterpolatedDepth((1ULL << 16) << 32 | 65536U);
    submitInterpolatedDepth((5ULL << 16) << 32 | 3ULL * 65536U);
    submitInterpolatedDepth((0x10000000ULL << 32) | 0x00010000ULL); // Z=0x8000, dZ/dX=1
    submitInterpolatedDepth(0ULL); // dZ/dE and dZ/dY
    const std::size_t interpolatedDepthPixel = 256 + (2 * 8 + 2) * 2;
    const std::uint16_t interpolatedDepthWord = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(interpolatedDepthMemory[interpolatedDepthPixel]) << 8U) |
        interpolatedDepthMemory[interpolatedDepthPixel + 1U]);
    check(interpolatedDepthWord != 0xFFFCU,
          "FillDepthTriangle interpolates pixel Z and updates the depth buffer");

    // ShadeDepthTriangle combines the RGBA and Z coefficient blocks.
    // This catches coefficient-offset mistakes when both blocks are present.
    Rdp shadedDepthTriangle;
    std::vector<std::uint8_t> shadedDepthMemory(8 * 8 * 4 + 8 * 8 * 2, 0);
    for (std::size_t i = 256; i < shadedDepthMemory.size(); i += 2)
    {
        shadedDepthMemory[i] = 0xFF;
        shadedDepthMemory[i + 1] = 0xFC;
    }
    const auto submitShadedDepth = [&shadedDepthTriangle, &shadedDepthMemory](std::uint64_t word) {
        if (shadedDepthTriangle.pushCommandWord(word))
            shadedDepthTriangle.executeLastCommand(shadedDepthMemory);
    };
    submitShadedDepth((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitShadedDepth((0x3EULL << 56) | 256ULL);
    submitShadedDepth((0x2FULL << 56) | 0x30ULL);
    submitShadedDepth((0x0DULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitShadedDepth((5ULL << 16) << 32 | 65536U);
    submitShadedDepth((1ULL << 16) << 32 | 65536U);
    submitShadedDepth((5ULL << 16) << 32 | 3ULL * 65536U);
    submitShadedDepth((128ULL << 16) << 32 | (64ULL << 16));
    submitShadedDepth((192ULL << 16) << 32 | (255ULL << 16));
    for (int i = 0; i < 6; ++i) submitShadedDepth(0ULL);
    submitShadedDepth((0x10000000ULL << 32) | 0x00010000ULL);
    submitShadedDepth(0ULL);
    const std::size_t shadedDepthColor = (2 * 8 + 2) * 4;
    const std::size_t shadedDepthZ = 256 + (2 * 8 + 2) * 2;
    const std::uint16_t shadedDepthWord = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(shadedDepthMemory[shadedDepthZ]) << 8U) |
        shadedDepthMemory[shadedDepthZ + 1U]);
    check(shadedDepthMemory[shadedDepthColor] == 0x80 &&
          shadedDepthMemory[shadedDepthColor + 1] == 0x40 &&
          shadedDepthMemory[shadedDepthColor + 2] == 0xC0 &&
          shadedDepthMemory[shadedDepthColor + 3] == 0xFF,
          "ShadeDepthTriangle writes interpolated RGBA");
    check(shadedDepthWord != 0xFFFCU,
          "ShadeDepthTriangle reads its Z block after the shade coefficients");

    // Transparent and decal Z modes may write color, but must preserve
    // the existing depth value even when Z-update is enabled.
    const auto renderSpecialZMode = [](std::uint32_t modes, std::uint32_t initialDepth) {
        Rdp rdp;
        std::vector<std::uint8_t> memory(8 * 8 * 4 + 8 * 8 * 2, 0);
        const std::uint16_t initialWord = static_cast<std::uint16_t>(
            Rdp::compressDepth(initialDepth) << 2U);
        for (std::size_t i = 256; i < memory.size(); i += 2)
        {
            memory[i] = static_cast<std::uint8_t>(initialWord >> 8U);
            memory[i + 1] = static_cast<std::uint8_t>(initialWord);
        }
        const auto submit = [&rdp, &memory](std::uint64_t word) {
            if (rdp.pushCommandWord(word))
                rdp.executeLastCommand(memory);
        };
        submit((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
        submit((0x3EULL << 56) | 256ULL);
        submit((0x2FULL << 56) | modes);
        submit((0x2EULL << 56) | (0x1000ULL << 16) | 1ULL);
        submit((0x37ULL << 56) | 0xFF0000FFULL);
        submit((0x08ULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
        submit((5ULL << 16) << 32 | 65536U);
        submit((1ULL << 16) << 32 | 65536U);
        submit((5ULL << 16) << 32 | 3ULL * 65536U);
        const std::size_t pixel = (2 * 8 + 2) * 4;
        const std::size_t zAddress = 256 + (2 * 8 + 2) * 2;
        const std::uint16_t finalDepth = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(memory[zAddress]) << 8U) | memory[zAddress + 1]);
        return std::pair<std::uint8_t, std::uint16_t>{memory[pixel], finalDepth};
    };
    const auto transparentMode = renderSpecialZMode(0x830U, 0x10000U);
    check(transparentMode.first == 0xFF &&
          transparentMode.second == static_cast<std::uint16_t>(
              Rdp::compressDepth(0x10000U) << 2U),
          "transparent Z mode writes nearer color without updating depth");
    const auto decalMode = renderSpecialZMode(0xC30U, 0x8000U);
    check(decalMode.first == 0xFF &&
          decalMode.second == static_cast<std::uint16_t>(
              Rdp::compressDepth(0x8000U) << 2U),
          "decal Z mode accepts coplanar color without updating depth");

    // The two hidden DeltaZ bits must survive an RDP depth write.
    // An exponent of 10 is split across the visible word and hidden bits;
    // a later decal 512 depth units away must still be considered coplanar.
    Rdp hiddenDeltaZ;
    std::vector<std::uint8_t> hiddenDeltaMemory(8 * 8 * 4 + 8 * 8 * 2, 0);
    const auto submitHiddenDelta = [&hiddenDeltaZ, &hiddenDeltaMemory](std::uint64_t word) {
        if (hiddenDeltaZ.pushCommandWord(word))
            hiddenDeltaZ.executeLastCommand(hiddenDeltaMemory);
    };
    submitHiddenDelta((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitHiddenDelta((0x3EULL << 56) | 256ULL);
    submitHiddenDelta((0x2FULL << 56) | 0x30ULL); // compare and update, primitive Z source
    submitHiddenDelta((0x2EULL << 56) | (0x1000ULL << 16) | 1024ULL);
    submitHiddenDelta((0x37ULL << 56) | 0xAA0000FFULL);
    submitHiddenDelta((0x08ULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitHiddenDelta((5ULL << 16) << 32 | 65536U);
    submitHiddenDelta((1ULL << 16) << 32 | 65536U);
    submitHiddenDelta((5ULL << 16) << 32 | 3ULL * 65536U);
    submitHiddenDelta((0x2FULL << 56) | 0xC30ULL); // decal compare, no Z update
    submitHiddenDelta((0x2EULL << 56) | (0x1040ULL << 16) | 1ULL); // Z differs by 512
    submitHiddenDelta((0x37ULL << 56) | 0xBB0000FFULL);
    submitHiddenDelta((0x08ULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitHiddenDelta((5ULL << 16) << 32 | 65536U);
    submitHiddenDelta((1ULL << 16) << 32 | 65536U);
    submitHiddenDelta((5ULL << 16) << 32 | 3ULL * 65536U);
    check(hiddenDeltaMemory[(2 * 8 + 2) * 4] == 0xBB,
          "decal comparison uses the hidden low bits of stored DeltaZ");

    // Exercise the rejecting sides of each depth mode, plus the
    // interpenetrating mode's DeltaZ tolerance.
    const auto renderDepthCase = [](std::uint32_t modes, std::uint16_t primitiveZ,
                                    std::uint16_t primitiveDelta, std::uint32_t storedZ) {
        Rdp rdp;
        std::vector<std::uint8_t> memory(8 * 8 * 4 + 8 * 8 * 2, 0);
        const std::uint16_t storedWord = static_cast<std::uint16_t>(
            Rdp::compressDepth(storedZ) << 2U);
        for (std::size_t i = 256; i < memory.size(); i += 2)
        {
            memory[i] = static_cast<std::uint8_t>(storedWord >> 8U);
            memory[i + 1] = static_cast<std::uint8_t>(storedWord);
        }
        const auto submit = [&rdp, &memory](std::uint64_t word) {
            if (rdp.pushCommandWord(word))
                rdp.executeLastCommand(memory);
        };
        submit((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
        submit((0x3EULL << 56) | 256ULL);
        submit((0x2FULL << 56) | modes);
        submit((0x2EULL << 56) | (static_cast<std::uint64_t>(primitiveZ) << 16U) |
               primitiveDelta);
        submit((0x37ULL << 56) | 0xCC0000FFULL);
        submit((0x08ULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
        submit((5ULL << 16) << 32 | 65536U);
        submit((1ULL << 16) << 32 | 65536U);
        submit((5ULL << 16) << 32 | 3ULL * 65536U);
        const std::size_t pixel = (2 * 8 + 2) * 4;
        return memory[pixel];
    };
    check(renderDepthCase(0x34U, 0x1020U, 1U, 0x8000U) == 0,
          "opaque Z mode rejects a fragment behind stored depth");
    check(renderDepthCase(0x434U, 0x1020U, 1024U, 0x8000U) == 0xCC,
          "interpenetrating Z mode accepts fragments inside DeltaZ tolerance");
    check(renderDepthCase(0x434U, 0x1100U, 16U, 0x8000U) == 0,
          "interpenetrating Z mode rejects fragments outside DeltaZ tolerance");
    check(renderDepthCase(0xC34U, 0x1100U, 16U, 0x8000U) == 0,
          "decal Z mode rejects fragments outside DeltaZ tolerance");
    check(renderDepthCase(0x834U, 0x1000U, 1U, 0x8000U) == 0,
          "transparent Z mode rejects fragments at equal depth");

    // Disabling depth comparison must not disable depth updates.
    Rdp updateWithoutCompare;
    std::vector<std::uint8_t> updateMemory(8 * 8 * 4 + 8 * 8 * 2, 0);
    const auto submitUpdate = [&updateWithoutCompare, &updateMemory](std::uint64_t word) {
        if (updateWithoutCompare.pushCommandWord(word))
            updateWithoutCompare.executeLastCommand(updateMemory);
    };
    submitUpdate((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitUpdate((0x3EULL << 56) | 256ULL);
    submitUpdate((0x2FULL << 56) | 0x24U); // update enabled, compare disabled, primitive Z
    submitUpdate((0x2EULL << 56) | (0x1020ULL << 16) | 1ULL);
    submitUpdate((0x37ULL << 56) | 0xDD0000FFULL);
    submitUpdate((0x08ULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitUpdate((5ULL << 16) << 32 | 65536U);
    submitUpdate((1ULL << 16) << 32 | 65536U);
    submitUpdate((5ULL << 16) << 32 | 3ULL * 65536U);
    const std::size_t updatePixel = (2 * 8 + 2) * 4;
    const std::size_t updateZAddress = 256 + (2 * 8 + 2) * 2;
    const std::uint16_t updatedDepthWord = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(updateMemory[updateZAddress]) << 8U) |
        updateMemory[updateZAddress + 1U]);
    check(updateMemory[updatePixel] == 0xDD &&
          (updatedDepthWord >> 2U) == Rdp::compressDepth(0x8100U),
          "depth update works when depth comparison is disabled");

    // Triangle rasterization must honor all four scissor boundaries.
    Rdp scissoredTriangle;
    std::vector<std::uint8_t> scissorMemory(8 * 8 * 4, 0);
    const auto submitScissor = [&scissoredTriangle, &scissorMemory](std::uint64_t word) {
        if (scissoredTriangle.pushCommandWord(word))
            scissoredTriangle.executeLastCommand(scissorMemory);
    };
    submitScissor((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitScissor((0x2DULL << 56) | (8ULL << 44) | (8ULL << 32) |
                  (12ULL << 12) | 12ULL);
    submitScissor((0x37ULL << 56) | 0xEE0000FFULL);
    submitScissor((0x08ULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitScissor((5ULL << 16) << 32 | 65536U);
    submitScissor((1ULL << 16) << 32 | 65536U);
    submitScissor((5ULL << 16) << 32 | 3ULL * 65536U);
    check(scissorMemory[(2 * 8 + 2) * 4] == 0xEE,
          "triangle writes pixels inside the scissor rectangle");
    check(scissorMemory[(2 * 8 + 1) * 4] == 0,
          "triangle rejects pixels left of the scissor rectangle");
    check(scissorMemory[(1 * 8 + 2) * 4] == 0,
          "triangle rejects pixels above the scissor rectangle");
    check(scissorMemory[(2 * 8 + 3) * 4] == 0,
          "triangle rejects pixels right of the scissor rectangle");
    check(scissorMemory[(3 * 8 + 2) * 4] == 0,
          "triangle rejects pixels below the scissor rectangle");

    // Reset must discard rasterizer state, including a previously narrowed scissor.
    Rdp resetScissor;
    std::vector<std::uint8_t> resetMemory(8 * 8 * 4, 0);
    const auto submitReset = [&resetScissor, &resetMemory](std::uint64_t word) {
        if (resetScissor.pushCommandWord(word))
            resetScissor.executeLastCommand(resetMemory);
    };
    submitReset((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitReset((0x2DULL << 56) | (12ULL << 44) | (8ULL << 32) |
                (16ULL << 12) | 12ULL);
    resetScissor.reset();
    submitReset((0x3FULL << 56) | (3ULL << 51) | (7ULL << 32));
    submitReset((0x37ULL << 56) | 0xAB0000FFULL);
    submitReset((0x08ULL << 56) | (20ULL << 32) | (12ULL << 16) | 4ULL);
    submitReset((5ULL << 16) << 32 | 65536U);
    submitReset((1ULL << 16) << 32 | 65536U);
    submitReset((5ULL << 16) << 32 | 3ULL * 65536U);
    check(resetMemory[(2 * 8 + 2) * 4] == 0xAB,
          "RDP reset restores the default full-width scissor rectangle");

    if (failures != 0)
    {
        std::cerr << failures << " RDP test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All RDP command-framing tests passed\n";
    return EXIT_SUCCESS;
}