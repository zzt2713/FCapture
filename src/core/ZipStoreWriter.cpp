#include "core/ZipStoreWriter.h"

#include <QDateTime>
#include <QFile>

namespace {

// CRC-32（IEEE 802.3），ZIP 规范要求
quint32 crc32(const QByteArray &data)
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }

    quint32 crc = 0xFFFFFFFFu;
    for (const char ch : data)
        crc = table[(crc ^ quint8(ch)) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void put16(QByteArray &out, quint16 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
}

void put32(QByteArray &out, quint32 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 24) & 0xFF));
}

} // namespace

void ZipStoreWriter::addFile(const QString &path, const QByteArray &data)
{
    Entry e;
    e.name = path.toUtf8();
    e.data = data;
    e.crc = crc32(data);
    m_entries.append(e);
}

bool ZipStoreWriter::writeTo(const QString &outputPath, QString *error)
{
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入：%1").arg(outputPath);
        return false;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const quint16 dosTime = quint16((now.time().hour() << 11) | (now.time().minute() << 5)
                                    | (now.time().second() / 2));
    const quint16 dosDate = quint16(((now.date().year() - 1980) << 9)
                                    | (now.date().month() << 5) | now.date().day());

    struct Central
    {
        QByteArray name;
        quint32 crc = 0;
        quint32 size = 0;
        quint32 offset = 0;
    };
    QVector<Central> central;
    central.reserve(m_entries.size());

    QByteArray out;
    for (const Entry &e : m_entries) {
        Central c;
        c.name = e.name;
        c.crc = e.crc;
        c.size = quint32(e.data.size());
        c.offset = quint32(out.size());
        central.append(c);

        // 本地文件头
        put32(out, 0x04034b50);
        put16(out, 20);              // version needed
        put16(out, 0x0800);          // flag: UTF-8 文件名
        put16(out, 0);               // 压缩方式 0 = store
        put16(out, dosTime);
        put16(out, dosDate);
        put32(out, e.crc);
        put32(out, quint32(e.data.size()));   // 压缩后大小
        put32(out, quint32(e.data.size()));   // 原始大小
        put16(out, quint16(e.name.size()));
        put16(out, 0);               // extra 长度
        out.append(e.name);
        out.append(e.data);
    }

    const quint32 centralStart = quint32(out.size());
    for (const Central &c : central) {
        put32(out, 0x02014b50);
        put16(out, 20);              // version made by
        put16(out, 20);              // version needed
        put16(out, 0x0800);          // UTF-8
        put16(out, 0);               // store
        put16(out, dosTime);
        put16(out, dosDate);
        put32(out, c.crc);
        put32(out, c.size);
        put32(out, c.size);
        put16(out, quint16(c.name.size()));
        put16(out, 0);               // extra
        put16(out, 0);               // comment
        put16(out, 0);               // disk number
        put16(out, 0);               // internal attrs
        put32(out, 0);               // external attrs
        put32(out, c.offset);
        out.append(c.name);
    }

    const quint32 centralSize = quint32(out.size()) - centralStart;
    put32(out, 0x06054b50);          // 中央目录结束
    put16(out, 0);
    put16(out, 0);
    put16(out, quint16(central.size()));
    put16(out, quint16(central.size()));
    put32(out, centralSize);
    put32(out, centralStart);
    put16(out, 0);                   // 注释长度

    const bool ok = file.write(out) == out.size();
    file.close();
    if (!ok && error)
        *error = QStringLiteral("写入 zip 内容失败：%1").arg(outputPath);
    return ok;
}
