#pragma once

#include <QImage>
#include <QRectF>
#include <QString>
#include <QVector>

// 二维码 / 条码识别（ZXing-C++ 封装）
//
// 业务层只依赖这个头，不直接引用 zxing。同步接口：
// 典型图片（选区截图）在几毫秒到几十毫秒内出结果，调用方自行决定放哪个线程。
namespace QrScanner {

struct Result
{
    QString text;         // 解码出的文本
    QString format;       // 码制名称，如 "QRCode" / "EAN-13"
    QRectF  position;     // 在原图中的位置（图片像素坐标）
};

// 识别图里所有二维码/条码（支持一图多码）
QVector<Result> scan(const QImage &image, bool tryHarder = true);

// 便利判断：是否至少识别到一个
bool hasAny(const QImage &image);

} // namespace QrScanner
