#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <QtCore/QSize>
#include <QtCore/QMetaType>

// 数据记录结构体：表示一条捕获后的原始或处理后数据
struct DataRecord {
    QString id;                // 唯一标识
    QDateTime timestamp;       // 生成/接收时间
    QString mimeType;          // 数据MIME类型
    QByteArray payload;        // 数据内容
    QSize size;                // 图像尺寸(可选)
    quint64 checksum = 0;      // 校验和

    bool isEmpty() const {
        return payload.isEmpty();
    }
};

Q_DECLARE_METATYPE(DataRecord)
