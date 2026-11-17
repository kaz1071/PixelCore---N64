#include "core/rdp/rdp.h"

#include <cstdlib>
#include <iostream>
#include <string>\n#include <vector>

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
    check(Rdp::commandWordCount(0x09) == 6, "shaded triangle packet has six words");
    check(Rdp::commandWordCount(0x0A) == 6, "textured triangle packet has six words");
    check(Rdp::commandWordCount(0x0B) == 8, "shaded textured triangle packet has eight words");
    check(Rdp::commandWordCount(0x0C) == 6, "depth triangle packet has six words");
    check(Rdp::commandWordCount(0x0D) == 8, "shaded depth triangle packet has eight words");
    check(Rdp::commandWordCount(0x0E) == 8, "textured depth triangle packet has eight words");
    check(Rdp::commandWordCount(0x0F) == 10, "full triangle packet has ten words");
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
        (0x08ULL << 56) | (4ULL << 32) | (4ULL << 16) | 20ULL;
    submit(triangleHeader);
    submit((1ULL << 48) | 0ULL); // xh=1, dxh/dy=0
    submit((5ULL << 48) | 0ULL); // xm=5, dxm/dy=0
    submit((1ULL << 48) | 0xFFFF0000ULL); // xl=1, dxl/dy=-1
    check(framebuffer[(2 * 8 + 2) * 4] == 0xFF &&
          framebuffer[(2 * 8 + 2) * 4 + 1] == 0,
          "edge-only triangle fills an interior pixel");
    check(framebuffer[(2 * 8 + 6) * 4] == 0,
          "edge-only triangle leaves pixels outside its edges untouched");

    if (failures != 0)
    {
        std::cerr << failures << " RDP test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All RDP command-framing tests passed\n";
    return EXIT_SUCCESS;
}