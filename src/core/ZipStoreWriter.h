#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

// 最简 ZIP 写入器：只做 store（不压缩），够用且零依赖。
// Qt 6.8 公开 API 没有 zip 写入能力（QZipWriter 是私有头且本构建未导出 CMake target），
// 诊断包体积很小，不压缩完全可接受。
class ZipStoreWriter
{
public:
    // 添加一个内存文件；内部路径统一用 / 分隔
    void addFile(const QString &path, const QByteArray &data);

    // 把已添加的文件写入 outputPath，覆盖同名文件
    bool writeTo(const QString &outputPath, QString *error = nullptr);

    bool isEmpty() const { return m_entries.isEmpty(); }

private:
    struct Entry
    {
        QByteArray name;    // UTF-8
        QByteArray data;
        quint32 crc = 0;
    };

    QVector<Entry> m_entries;
};
