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

    rdp.reset();
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
    check(framebuffer[(2 * 8 + 6) * 4] == 0,
          "edge-only triangle leaves pixels outside its edges untouched");

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
    check(framebuffer16[(2 * 8 + 6) * 2] == 0,
          "RGBA16 triangle clips pixels beyond its right edge");

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

    if (failures != 0)
    {
        std::cerr << failures << " RDP test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All RDP command-framing tests passed\n";
    return EXIT_SUCCESS;
}