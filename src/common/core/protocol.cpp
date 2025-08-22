#include "protocol.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include "logging_categories.h"
#include <QtCore/QtEndian>
#include <zlib.h>
#include <QtCore/QMessageLogger>

const QByteArray Protocol::XORkey = "3fG7qR9TkL2pY8xN";

QByteArray Protocol::createMessage(MessageType type, const IMessageCodec &message)
{
    QByteArray payload = message.encode();
    MessageHeader header;
    // 设置协议基础字段，确保可被正确解析
    header.magic = PROTOCOL_MAGIC;          // 魔数
    header.version = PROTOCOL_VERSION;      // 协议版本
    header.type = type;                     // 消息类型
    header.length = static_cast<quint32>(payload.size()); // 数据长度
    header.timestamp = QDateTime::currentMSecsSinceEpoch(); // 时间戳
    header.checksum = calculateChecksum(payload);           // 校验和

    QByteArray headerData = header.encode();
    QByteArray messageData = headerData + payload;

    // 加密数据
    QByteArray encryptedData = encryptData(messageData, Protocol::XORkey);

    return encryptedData;
}

bool Protocol::parseMessage(const QByteArray &data, MessageHeader &header, QByteArray &payload)
{
    if (data.size() < static_cast<qsizetype>(SERIALIZED_HEADER_SIZE)) {
        return false;
    }

    // 解密数据
    QByteArray encryptedData = decryptData(data, Protocol::XORkey);
    if (encryptedData.size() < static_cast<qsizetype>(SERIALIZED_HEADER_SIZE)) {
        return false;
    }

    // 反序列化消息头
    QByteArray headerData = encryptedData.left(static_cast<qsizetype>(SERIALIZED_HEADER_SIZE));
    if (header.decode(headerData) == false) {
        return false;
    }
    
    // 检查数据长度
    qsizetype expectedSize = static_cast<qsizetype>(SERIALIZED_HEADER_SIZE) + header.length;
    if (encryptedData.size() < expectedSize) {
        return false;
    }
    
    // 提取载荷
    payload = encryptedData.mid(static_cast<qsizetype>(SERIALIZED_HEADER_SIZE), header.length);

    // 验证消息
    if (validateMessage(header, payload) == false) {
        return false;
    }
    return true;
}

QByteArray Protocol::encryptData(const QByteArray &data, const QByteArray &key)
{
    // 简单的XOR加密（实际应用中应使用AES等强加密算法）
    if (data.isEmpty() || key.isEmpty()) {
        return data;
    }
    
    QByteArray encrypted = data;
    for (int i = 0; i < encrypted.size(); ++i) {
        encrypted[i] = encrypted[i] ^ key[i % key.size()];
    }
    
    return encrypted;
}

QByteArray Protocol::decryptData(const QByteArray &data, const QByteArray &key)
{
    // XOR加密的解密就是再次XOR
    return encryptData(data, key);
}

bool Protocol::validateMessage(const MessageHeader &header, const QByteArray &payload)
{
    // 验证魔数
    if (header.magic != PROTOCOL_MAGIC) {
        return false;
    }
    
    // 验证版本
    if (header.version != PROTOCOL_VERSION) {
        return false;
    }
    
    // 验证长度
    if (header.length != static_cast<quint32>(payload.size())) {
        return false;
    }
    
    // 验证校验和
    quint32 calculatedChecksum = calculateChecksum(payload);
    if (calculatedChecksum != header.checksum) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol) << "Checksum mismatch. Expected:" << Qt::hex << header.checksum
                   << "Calculated:" << Qt::hex << calculatedChecksum
                   << "Payload size:" << payload.size()
                   << "Payload hex:" << payload.toHex();
        return false;
    }
    
    return true;
}

quint32 Protocol::calculateChecksum(const QByteArray &data)
{
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(data);
    QByteArray result = hash.result();
    
    quint32 checksum = 0;
    if (result.size() >= 4) {
        checksum = qFromLittleEndian<quint32>(result.constData());
    }
    
    return checksum;
}