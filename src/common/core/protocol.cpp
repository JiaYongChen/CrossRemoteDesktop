#include "protocol.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QtEndian>
#include <zlib.h>

quint32 Protocol::s_sequenceNumber = 0;

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

QByteArray Protocol::serializeHeader(const MessageHeader &header)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    stream << header.magic;
    stream << header.version;
    stream << static_cast<quint32>(header.type);
    stream << header.length;
    stream << header.sequence;
    stream << header.checksum;
    stream << header.timestamp;
    
    return data;
}

bool Protocol::deserializeHeader(const QByteArray &data, MessageHeader &header)
{
    if (data.size() < static_cast<qsizetype>(SERIALIZED_HEADER_SIZE)) {
        return false;
    }
    
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    quint32 typeValue;
    stream >> header.magic;
    stream >> header.version;
    stream >> typeValue;
    stream >> header.length;
    stream >> header.sequence;
    stream >> header.checksum;
    stream >> header.timestamp;
    
    header.type = static_cast<MessageType>(typeValue);
    
    // 验证魔数
    if (header.magic != PROTOCOL_MAGIC) {
        qWarning() << "无效的魔数:" << Qt::hex << header.magic;
        return false;
    }
    
    // 验证版本
    if (header.version != PROTOCOL_VERSION) {
        qWarning() << "不支持的协议版本:" << header.version;
        return false;
    }
    
    return true;
}

QByteArray Protocol::createMessage(MessageType type, const QByteArray &payload)
{
    MessageHeader header;
    header.type = type;
    header.length = payload.size();
    header.sequence = ++s_sequenceNumber;
    header.timestamp = QDateTime::currentMSecsSinceEpoch();
    header.checksum = calculateChecksum(payload);
    
    QByteArray headerData = serializeHeader(header);
    QByteArray message = headerData + payload;
    
    return message;
}

bool Protocol::parseMessage(const QByteArray &data, MessageHeader &header, QByteArray &payload)
{
    if (data.size() < static_cast<qsizetype>(SERIALIZED_HEADER_SIZE)) {
        return false;
    }
    
    // 反序列化消息头
    QByteArray headerData = data.left(static_cast<qsizetype>(SERIALIZED_HEADER_SIZE));
    if (!deserializeHeader(headerData, header)) {
        return false;
    }
    
    // 检查数据长度
    qsizetype expectedSize = static_cast<qsizetype>(SERIALIZED_HEADER_SIZE) + header.length;
    if (data.size() < expectedSize) {
        return false;
    }
    
    // 提取载荷
    payload = data.mid(static_cast<qsizetype>(SERIALIZED_HEADER_SIZE), header.length);
    
    // 验证校验和
    quint32 calculatedChecksum = calculateChecksum(payload);
    if (calculatedChecksum != header.checksum) {
        qWarning() << "Checksum mismatch. Expected:" << Qt::hex << header.checksum
                   << "Calculated:" << Qt::hex << calculatedChecksum
                   << "Payload size:" << payload.size()
                   << "Payload hex:" << payload.toHex();
        return false;
    }
    
    return true;
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
        return false;
    }
    
    return true;
}

QByteArray Protocol::compressData(const QByteArray &data, int level)
{
    if (data.isEmpty()) {
        return QByteArray();
    }
    
    // 使用zlib压缩
    uLongf compressedSize = compressBound(data.size());
    QByteArray compressed(compressedSize, 0);
    
    int result = compress2(reinterpret_cast<Bytef*>(compressed.data()),
                          &compressedSize,
                          reinterpret_cast<const Bytef*>(data.constData()),
                          data.size(),
                          level);
    
    if (result != Z_OK) {
        qWarning() << "Compression failed with error:" << result;
        return QByteArray();
    }
    
    compressed.resize(compressedSize);
    return compressed;
}

QByteArray Protocol::decompressData(const QByteArray &data)
{
    if (data.isEmpty()) {
        return QByteArray();
    }
    
    // 尝试不同的解压缩大小
    for (int factor = 2; factor <= 32; factor *= 2) {
        uLongf uncompressedSize = data.size() * factor;
        QByteArray uncompressed(uncompressedSize, 0);
        
        int result = uncompress(reinterpret_cast<Bytef*>(uncompressed.data()),
                               &uncompressedSize,
                               reinterpret_cast<const Bytef*>(data.constData()),
                               data.size());
        
        if (result == Z_OK) {
            uncompressed.resize(uncompressedSize);
            return uncompressed;
        } else if (result != Z_BUF_ERROR) {
            qWarning() << "Decompression failed with error:" << result;
            break;
        }
    }
    
    return QByteArray();
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