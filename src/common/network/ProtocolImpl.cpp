#include "Protocol.h"

#include <QtCore/QDateTime>

#include "common/config/NetworkConstants.h"
#include "common/config/ProtocolConstants.h"
#include "common/logging/LoggingCategories.h"

// Protocol 类的静态函数实现（TLS负责传输层加密，协议层不再加密）
QByteArray Protocol::createMessage(MessageType type, const IMessageCodec& message) {
    // 步骤1：编码消息载荷（明文，由TLS保护）
    QByteArray payload = message.encode();

    // 步骤2：构建消息头
    MessageHeader header;
    header.magic = ProtocolConstants::ProtocolMagic;
    header.version = ProtocolConstants::ProtocolVersion;
    header.type = type;
    header.length = static_cast<quint32>(payload.size());
    header.timestamp = QDateTime::currentMSecsSinceEpoch();
    header.checksum = calculateChecksum(payload);
    QByteArray headerData = header.encode();

    // 步骤3：组合完整消息
    return headerData + payload;
}

qsizetype Protocol::parseMessage(const QByteArray& data, MessageHeader& header, QByteArray& payload) {
    // 步骤1：验证数据完整性，同时获取MessageHeader
    qsizetype validationResult = validateReceivedDataIntegrity(data, header);
    if ( validationResult <= 0 ) {
        return validationResult;
    }

    // 步骤2：获取消息载荷（明文，由TLS保护）
    payload = data.mid(static_cast<qsizetype>(ProtocolConstants::SerializedHeaderSize), static_cast<qsizetype>(header.length));

    return validationResult;
}

qsizetype Protocol::validateReceivedDataIntegrity(const QByteArray& data, MessageHeader& header) {
    // 步骤1：检查数据是否足够包含消息头
    if ( data.size() < static_cast<qsizetype>(ProtocolConstants::SerializedHeaderSize) ) {
        return -1;
    }

    // 步骤2：反序列化消息头（明文，由TLS保护）
    QByteArray headerData = data.left(static_cast<qsizetype>(ProtocolConstants::SerializedHeaderSize));
    if ( !header.decode(headerData) ) {
        qCWarning(lcCoreProtocol) << "Protocol::validateReceivedDataIntegrity() - Failed to parse message header";
        return 0;
    }

    // 步骤3：验证魔数
    if ( header.magic != ProtocolConstants::ProtocolMagic ) {
        qCWarning(lcCoreProtocol) << "Protocol::validateReceivedDataIntegrity() - Invalid magic number:" << Qt::hex << header.magic
            << "expected:" << Qt::hex << ProtocolConstants::ProtocolMagic;
        return 0;
    }

    // 步骤4：验证协议版本
    if ( header.version != ProtocolConstants::ProtocolVersion ) {
        qCWarning(lcCoreProtocol) << "Protocol::validateReceivedDataIntegrity() - Unsupported protocol version:" << header.version
            << "expected:" << ProtocolConstants::ProtocolVersion;
        return 0;
    }

    // 步骤5：检查payload长度是否合理（防止恶意超大消息）
    const quint32 MAX_PAYLOAD_SIZE = NetworkConstants::MaxPacketSize - ProtocolConstants::SerializedHeaderSize;
    if ( header.length > MAX_PAYLOAD_SIZE ) {
        qCWarning(lcCoreProtocol) << "Protocol::validateReceivedDataIntegrity() - Payload size too large:" << header.length
            << "max allowed:" << MAX_PAYLOAD_SIZE;
        return 0;
    }

    // 步骤6：计算完整消息需要的总长度
    qsizetype totalMessageSize = static_cast<qsizetype>(ProtocolConstants::SerializedHeaderSize) + static_cast<qsizetype>(header.length);

    // 步骤7：检查当前接收的数据是否包含完整消息
    if ( data.size() < totalMessageSize ) {
        return -1;
    }

    // 步骤8：验证校验和
    QByteArray payload = data.mid(static_cast<qsizetype>(ProtocolConstants::SerializedHeaderSize), static_cast<qsizetype>(header.length));
    quint32 calculatedChecksum = calculateChecksum(payload);
    if ( calculatedChecksum != header.checksum ) {
        qCWarning(lcCoreProtocol)
            << "Checksum mismatch. Expected:" << Qt::hex << header.checksum
            << "Calculated:" << Qt::hex << calculatedChecksum;
        return 0;
    }

    return totalMessageSize;
}

// CRC-32 lookup table (ISO 3309 / ITU-T V.42, polynomial 0xEDB88320)
// Generated at compile time for zero runtime cost.
static constexpr std::array<quint32, 256> generateCrc32Table() {
    std::array<quint32, 256> table{};
    for ( quint32 i = 0; i < 256; ++i ) {
        quint32 crc = i;
        for ( int j = 0; j < 8; ++j ) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
        }
        table[i] = crc;
    }
    return table;
}

static constexpr auto kCrc32Table = generateCrc32Table();

quint32 Protocol::calculateChecksum(const QByteArray& data) {
    // CRC-32: purpose-appropriate for integrity checks, ~10x faster than MD5.
    // Note: this is NOT a security hash — TLS handles authentication.
    quint32 crc = 0xFFFFFFFFu;
    const auto* bytes = reinterpret_cast<const quint8*>(data.constData());
    const auto len = static_cast<qsizetype>(data.size());
    for ( qsizetype i = 0; i < len; ++i ) {
        crc = kCrc32Table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}
