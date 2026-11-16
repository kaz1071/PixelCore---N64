#include "core/rom/rom_file.h"

#include <QByteArray>
#include <QFile>

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

    const QByteArray header = file.read(0x40);
    if (header.size() != 0x40) {
        error = "Could not read the complete Nintendo 64 ROM header.";
        return false;
    }

    const auto byte = [&header](qsizetype index) {
        return static_cast<unsigned char>(header.at(index));
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
        break;
    case 0x40123780:
        byteOrder_ = N64ByteOrder::LittleEndian;
        break;
    default:
        error = QString("Unrecognized ROM header magic: 0x%1")
                    .arg(QString::number(magic, 16).rightJustified(8, QLatin1Char('0'))
                             .toUpper());
        return false;
    }

    QByteArray rawTitle = header.mid(0x20, 20);
    if (byteOrder_ == N64ByteOrder::ByteSwapped) {
        for (qsizetype i = 0; i + 1 < rawTitle.size(); i += 2) {
            const char first = rawTitle.at(i);
            rawTitle[i] = rawTitle.at(i + 1);
            rawTitle[i + 1] = first;
        }
    } else if (byteOrder_ == N64ByteOrder::LittleEndian) {
        for (qsizetype i = 0; i + 3 < rawTitle.size(); i += 4) {
            const char first = rawTitle.at(i);
            const char second = rawTitle.at(i + 1);
            rawTitle[i] = rawTitle.at(i + 3);
            rawTitle[i + 1] = rawTitle.at(i + 2);
            rawTitle[i + 2] = second;
            rawTitle[i + 3] = first;
        }
    }

    title_ = QString::fromLatin1(rawTitle).trimmed();
    fileSize_ = file.size();
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
