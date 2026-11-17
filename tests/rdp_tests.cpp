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
    check(rdp.tile(3).loaded && rdp.tile(3).loadType == 1,
          "LoadBlock marks the selected tile loaded with block mode");
    check(rdp.tile(3).loadStartS == 0x111 && rdp.tile(3).loadStartT == 0x222 &&
          rdp.tile(3).loadEndS == 0x333 && rdp.tile(3).loadEndT == 0 &&
          rdp.tile(3).loadDxt == 0x444, "LoadBlock decodes coordinates and DXT");
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

    check(rdp.fogColor() == 0x10203040U, "SetFogColor stores packed RGBA");
    check(rdp.blendColor() == 0x50607080U, "SetBlendColor stores packed RGBA");
    check(rdp.primitiveColor() == 0x90A0B0C0U, "SetPrimitiveColor stores packed RGBA");
    check(rdp.primitiveMinLevel() == 0x35U, "SetPrimitiveColor decodes minimum LOD level");
    check(rdp.primitiveLodFraction() == 0x79U, "SetPrimitiveColor decodes primitive LOD fraction");
    check(rdp.environmentColor() == 0xD0E0F001U, "SetEnvironmentColor stores packed RGBA");

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