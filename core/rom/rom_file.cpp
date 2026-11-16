#include "core/rom/rom_file.h"

#include <QByteArray>
#include <QFile>

#include <algorithm>

bool N64Rom::load(const QString &path, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = "The selected file could not be opened:\n" + file.errorString();
        return false;
    }

    if (file.size() < 0x40) {
        error = "This file is too small to contain a Nintendo 64 ROM header.";
        return false;
    }

    const QByteArray raw = file.readAll();
    if (raw.size() < 0x40) {
        error = "Could not read the complete Nintendo 64 ROM image.";
        return false;
    }

    const auto byte = [&raw](qsizetype index) {
        return static_cast<unsigned char>(raw.at(index));
    };

    const unsigned int magic =
        (static_cast<unsigned int>(byte(0)) << 24) |
        (static_cast<unsigned int>(byte(1)) << 16) |
        (static_cast<unsigned int>(byte(2)) << 8) |
        static_cast<unsigned int>(byte(3));

    switch (magic) {
    case 0x80371240:
        byteOrder_ = N64ByteOrder::BigEndian;
        break;
    case 0x37804012:
        byteOrder_ = N64ByteOrder::ByteSwapped;
        if ((raw.size() % 2) != 0) {
            error = "The byte-swapped ROM has an incomplete 16-bit byte pair.";
            return false;
        }
        break;
    case 0x40123780:
        byteOrder_ = N64ByteOrder::LittleEndian;
        if ((raw.size() % 4) != 0) {
            error = "The little-endian ROM size is not a multiple of four bytes.";
            return false;
        }
        break;
    default:
        error = QString("Unrecognized ROM header magic: 0x%1")
                    .arg(QString::number(magic, 16).rightJustified(8, QLatin1Char('0'))
                             .toUpper());
        return false;
    }

    image_.resize(static_cast<std::size_t>(raw.size()));
    for (qsizetype i = 0; i < raw.size(); ++i)
        image_[static_cast<std::size_t>(i)] = byte(i);

    if (byteOrder_ == N64ByteOrder::ByteSwapped) {
        for (std::size_t i = 0; i < image_.size(); i += 2)
            std::swap(image_[i], image_[i + 1]);
    } else if (byteOrder_ == N64ByteOrder::LittleEndian) {
        for (std::size_t i = 0; i < image_.size(); i += 4)
            std::reverse(image_.begin() + static_cast<std::ptrdiff_t>(i),
                         image_.begin() + static_cast<std::ptrdiff_t>(i + 4));
    }

    QByteArray rawTitle;
    rawTitle.resize(20);
    for (qsizetype i = 0; i < 20; ++i)
        rawTitle[i] = static_cast<char>(image_[0x20 + static_cast<std::size_t>(i)]);

    title_ = QString::fromLatin1(rawTitle).trimmed();
    fileSize_ = raw.size();
    error.clear();
    return true;
}

QString N64Rom::title() const
{
    return title_;
}

QString N64Rom::byteOrderName() const
{
    switch (byteOrder_) {
    case N64ByteOrder::BigEndian:
        return "Big-endian (.z64)";
    case N64ByteOrder::ByteSwapped:
        return "Byte-swapped (.v64)";
    case N64ByteOrder::LittleEndian:
        return "Little-endian (.n64)";
    }

    return "Unknown";
}

qint64 N64Rom::fileSize() const
{
    return fileSize_;
}

const std::vector<std::uint8_t> &N64Rom::image() const
{
    return image_;
}
