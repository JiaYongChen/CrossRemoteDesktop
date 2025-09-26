#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <QtCore/QSize>
#include <QtCore/QMetaType>

// 数据记录结构体：表示一条捕获后的原始或处理后数据
// 说明：
// - 使用轻量的值类型，便于在信号槽或容器中传递
// - 包含基础元数据（ID、时间戳、MIME类型、尺寸信息、校验）
// - payload 保存原始或清洗/格式化后的二进制数据
struct DataRecord {
    QString id;                // 唯一标识，用于存储和检索
    QDateTime timestamp;       // 生成/接收时间
    QString mimeType;          // 数据MIME类型，例如 image/png, application/octet-stream 等
    QByteArray payload;        // 数据内容
    QSize size;                // 若为图像类数据：宽高（可选）
    quint64 checksum = 0;      // 简单一致性校验（例如SHA-256的前若干位截断）

    // 工具方法：是否为空记录
    bool isEmpty() const {
        return payload.isEmpty();
    }
};

Q_DECLARE_METATYPE(DataRecord)