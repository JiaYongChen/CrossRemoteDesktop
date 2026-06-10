#pragma once

#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QSize>

/**
 * @brief 数据记录结构体
 *
 * 用于验证、清洗、格式化与存储阶段之间传递数据。
 */
struct DataRecord {
    QString   id;          ///< 唯一标识符
    QDateTime timestamp;   ///< 时间戳
    QString   mimeType;    ///< MIME 类型
    QByteArray payload;    ///< 数据负载
    quint64   checksum = 0; ///< 校验和
    QSize     size;        ///< 尺寸（图像类数据）
};
