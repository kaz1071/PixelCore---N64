#pragma once

#include <QString>
#include <QtGlobal>

enum class N64ByteOrder
{
    BigEndian,
    ByteSwapped,
    LittleEndian
};

class N64Rom
{
public:
    bool load(const QString &path, QString &error);

    QString title() const;
    QString byteOrderName() const;
    qint64 fileSize() const;

private:
    QString title_;
    N64ByteOrder byteOrder_ = N64ByteOrder::BigEndian;
    qint64 fileSize_ = 0;
};
