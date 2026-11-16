#include "core/rom/rom_file.h"

#include <QTemporaryFile>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

static bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

static std::vector<std::uint8_t> encoded(
    const std::vector<std::uint8_t> &canonical, N64ByteOrder order)
{
    auto result = canonical;
    if (order == N64ByteOrder::ByteSwapped) {
        for (std::size_t i = 0; i < result.size(); i += 2)
            std::swap(result[i], result[i + 1]);
    } else if (order == N64ByteOrder::LittleEndian) {
        for (std::size_t i = 0; i < result.size(); i += 4)
            std::reverse(result.begin() + static_cast<std::ptrdiff_t>(i),
                         result.begin() + static_cast<std::ptrdiff_t>(i + 4));
    }
    return result;
}

static bool testOrder(N64ByteOrder order)
{
    std::vector<std::uint8_t> canonical(0x80, 0);
    canonical[0] = 0x80;
    canonical[1] = 0x37;
    canonical[2] = 0x12;
    canonical[3] = 0x40;
    const char title[] = "PIXELCORE TEST";
    std::copy(title, title + sizeof(title) - 1, canonical.begin() + 0x20);
    canonical[0x40] = 0x24;
    canonical[0x41] = 0x08;
    canonical[0x42] = 0x00;
    canonical[0x43] = 0x2A;

    const auto fileBytes = encoded(canonical, order);
    QTemporaryFile file;
    if (!file.open()) return false;
    const QByteArray bytes(reinterpret_cast<const char *>(fileBytes.data()),
                           static_cast<qsizetype>(fileBytes.size()));
    if (file.write(bytes) != bytes.size()) return false;
    file.flush();

    N64Rom rom;
    QString error;
    if (!rom.load(file.fileName(), error)) {
        std::cerr << "ROM load failed: " << error.toStdString() << '\n';
        return false;
    }
    return rom.image() == canonical && rom.title() == "PIXELCORE TEST";
}

int main()
{
    bool passed = true;
    passed &= check(testOrder(N64ByteOrder::BigEndian),
                    "big-endian z64 data remains canonical");
    passed &= check(testOrder(N64ByteOrder::ByteSwapped),
                    "byte-swapped v64 data is normalized");
    passed &= check(testOrder(N64ByteOrder::LittleEndian),
                    "little-endian n64 data is normalized");

    if (!passed) return 1;
    std::cout << "All N64 ROM normalization tests passed.\n";
    return 0;
}
