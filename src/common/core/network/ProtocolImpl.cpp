#include "Protocol.h"
#include <QtCore/QDataStream>
#include <QtCore/QIODevice>
#include <QtCore/QString>
#include <QtCore/QDebug>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QtEndian>
#include <QtCore/QMessageLogger>
#include "../logging/LoggingCategories.h"
#include "../config/NetworkConstants.h"
#include <cstring>
#include <zlib.h>

// Protocol 类的静态成员
const QByteArray Protocol::XORkey = "3fG7qR9TkL2pY8xN";

// Protocol 类的静态函数实现
QByteArray Protocol::createMessage(MessageType type, const IMessageCodec& message) {
    // 步骤1：编码并加密消息载荷
    QByteArray payload = encryptData(message.encode(), Protocol::XORkey); // 加密数据

    // 步骤2：构建消息头
    MessageHeader header;
    // 设置协议基础字段，确保可被正确解析
    header.magic = PROTOCOL_MAGIC;          // 魔数
    header.version = PROTOCOL_VERSION;      // 协议版本
    header.type = type;                     // 消息类型
    header.length = static_cast<quint32>(payload.size()); // 数据长度
    header.timestamp = QDateTime::currentMSecsSinceEpoch(); // 时间戳
    header.checksum = calculateChecksum(payload);           // 校验和
    QByteArray headerData = encryptData(header.encode(), Protocol::XORkey); // 加密数据

    // 步骤3：组合完整消息
    QByteArray messageData = headerData + payload; // 头部 + 载荷

    return messageData;
}

qsizetype Protocol::parseMessage(const QByteArray& data, MessageHeader& header, QByteArray& payload) {
    // 步骤1：验证数据完整性 , 同时获取MessageHeader
    qsizetype validationResult = validateReceivedDataIntegrity(data, header);
    if ( validationResult <= 0 ) {
        // 数据不完整或无效
        return validationResult;
    }

    // 步骤2：获取加密的消息数据,并解密
    QByteArray encryptedMessage = data.mid(static_cast<qsizetype>(SERIALIZED_HEADER_SIZE), validationResult);
    payload = decryptData(encryptedMessage, Protocol::XORkey);

    return validationResult;
}

qsizetype Protocol::validateReceivedDataIntegrity(const QByteArray& data, MessageHeader& header) {
    // 步骤1：检查数据是否足够包含消息头
    if ( data.size() < static_cast<qsizetype>(SERIALIZED_HEADER_SIZE) ) {
        // 数据不完整，需要等待更多数据
        return -1;
    }

    // 步骤2：解密消息头
    QByteArray encryptedHeader = data.left(static_cast<qsizetype>(SERIALIZED_HEADER_SIZE));
    QByteArray decryptedHeader = decryptData(encryptedHeader, Protocol::XORkey);

    // 步骤3：反序列化消息头
    if ( !header.decode(decryptedHeader) ) {
        // 消息头解析失败，数据无效
        qWarning(lcProtocol) << "消息头解析失败";
        return 0;
    }

    // 步骤4：验证魔数
    if ( header.magic != PROTOCOL_MAGIC ) {
        qWarning(lcProtocol) << "无效的魔数:" << Qt::hex << header.magic
            << "期望:" << Qt::hex << PROTOCOL_MAGIC;
        return 0;
    }

    // 步骤5：验证协议版本
    if ( header.version != PROTOCOL_VERSION ) {
        qWarning(lcProtocol) << "不支持的协议版本:" << header.version
            << "期望:" << PROTOCOL_VERSION;
        return 0;
    }

    // 步骤6：检查payload长度是否合理（防止恶意超大消息）
    const quint32 MAX_PAYLOAD_SIZE = NetworkConstants::MAX_PACKET_SIZE - SERIALIZED_HEADER_SIZE;
    if ( header.length > MAX_PAYLOAD_SIZE ) {
        qWarning(lcProtocol) << "Payload长度过大:" << header.length
            << "最大允许:" << MAX_PAYLOAD_SIZE;
        return 0;
    }

    // 步骤7：计算完整消息需要的总长度
    qsizetype totalMessageSize = static_cast<qsizetype>(SERIALIZED_HEADER_SIZE) + static_cast<qsizetype>(header.length);

    // 步骤8：检查当前接收的数据是否包含完整消息
    if ( data.size() < totalMessageSize ) {
        // 数据不完整，需要等待更多数据
        // 返回负值表示还需要接收的字节数（可选：返回-1表示不完整，或返回具体差值）
        return -1;
    }

    // 步骤9：验证校验和
    // 修复：使用 header.length 而不是 totalMessageSize，因为 mid() 的第二个参数是长度
    QByteArray payload = data.mid(static_cast<qsizetype>(SERIALIZED_HEADER_SIZE), static_cast<qsizetype>(header.length));
    quint32 calculatedChecksum = calculateChecksum(payload);
    if ( calculatedChecksum != header.checksum ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol)
            << "Checksum mismatch. Expected:" << Qt::hex << header.checksum
            << "Calculated:" << Qt::hex << calculatedChecksum;
        return 0;
    }

    // 步骤10：数据完整，header已填充，返回完整消息的总长度
    // qCDebug(lcProtocol) << "接收到完整消息，总长度:" << totalMessageSize
    //     << "bytes (Header:" << SERIALIZED_HEADER_SIZE
    //     << "+ Payload:" << header.length << ")"
    //     << "消息类型:" << static_cast<quint32>(header.type);
    return totalMessageSize;
}

QByteArray Protocol::encryptData(const QByteArray& data, const QByteArray& key) {
    // 简单的XOR加密（实际应用中应使用AES等强加密算法）
    if ( data.isEmpty() || key.isEmpty() ) {
        return data;
    }

    QByteArray encrypted = data;

    // 加密前
    //qCDebug(lcProtocol) << "加密前数据:" << encrypted.toHex();

    for ( int i = 0; i < encrypted.size(); ++i ) {
        encrypted[i] = encrypted[i] ^ key[i % key.size()];
    }

    // 加密后
    //qCDebug(lcProtocol) << "加密后数据:" << encrypted.toHex();

    return encrypted;
}

QByteArray Protocol::decryptData(const QByteArray& data, const QByteArray& key) {
    // XOR加密的解密就是再次XOR
    return encryptData(data, key);
}

quint32 Protocol::calculateChecksum(const QByteArray& data) {
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(data);
    QByteArray result = hash.result();

    quint32 checksum = 0;
    if ( result.size() >= 4 ) {
        checksum = qFromLittleEndian<quint32>(result.constData());
    }

    return checksum;
}

// 辅助函数：写入定长字符串（小端）
static void writeFixedStringLE(QDataStream& ds, const QString& s, int fixedLen) {
    QByteArray utf8 = s.toUtf8();
    if ( utf8.size() > fixedLen ) utf8.truncate(fixedLen);
    // 写入定长字节，未满补零
    if ( !utf8.isEmpty() ) ds.writeRawData(utf8.constData(), utf8.size());
    int pad = fixedLen - utf8.size();
    if ( pad > 0 ) {
        QByteArray zero(pad, '\0');
        ds.writeRawData(zero.constData(), zero.size());
    }
}

// 辅助函数：读取定长字符串（小端）
static QString readFixedStringLE(QDataStream& ds, int fixedLen) {
    QByteArray buf(fixedLen, 0);
    int read = ds.readRawData(buf.data(), fixedLen);
    if ( read != fixedLen ) return QString();
    int nul = buf.indexOf('\0');
    if ( nul >= 0 ) buf.truncate(nul);
    return QString::fromUtf8(buf);
}

// MessageHeader 序列化和反序列化实现
QByteArray MessageHeader::encode() const {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << magic;
    stream << version;
    stream << static_cast<quint32>(type);
    stream << length;
    stream << checksum;
    stream << timestamp;

    return data;
}

bool MessageHeader::decode(const QByteArray& data) {
    if ( data.size() < static_cast<qsizetype>(SERIALIZED_HEADER_SIZE) ) {
        return false;
    }

    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint32 typeValue;
    stream >> magic;
    stream >> version;
    stream >> typeValue;
    stream >> length;
    stream >> checksum;
    stream >> timestamp;

    type = static_cast<MessageType>(typeValue);

    if ( stream.status() != QDataStream::Ok ) return false;

    return true;
}

QByteArray BaseMessage::encode() const {
    QByteArray data;
    QDataStream ds(&data, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    if ( !this->data.isEmpty() ) ds.writeRawData(this->data.constData(), this->data.size());

    return data;
}

bool BaseMessage::decode(const QByteArray& data) {
    this->data = data.mid(qsizetype(0), data.size());
    return true;
}

// HandshakeRequest 序列化和反序列化实现
QByteArray HandshakeRequest::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << clientVersion;
    ds << screenWidth;
    ds << screenHeight;
    ds << colorDepth;
    writeFixedStringLE(ds, QString::fromUtf8(clientName), 64);
    writeFixedStringLE(ds, QString::fromUtf8(clientOS), 32);
    return bytes;
}

bool HandshakeRequest::decode(const QByteArray& bytes) {
    if ( bytes.size() < (4 + 2 + 2 + 1 + 64 + 32) ) return false;  // 减少1字节
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds >> clientVersion;
    ds >> screenWidth;
    ds >> screenHeight;
    ds >> colorDepth;
    QString name = readFixedStringLE(ds, 64);
    QString os = readFixedStringLE(ds, 32);
    if ( ds.status() != QDataStream::Ok ) return false;
    memset(clientName, 0, sizeof(clientName));
    memset(clientOS, 0, sizeof(clientOS));
    qstrncpy(clientName, name.toUtf8().constData(), int(sizeof(clientName)) - 1);
    qstrncpy(clientOS, os.toUtf8().constData(), int(sizeof(clientOS)) - 1);
    return true;
}

// HandshakeResponse 序列化和反序列化实现
QByteArray HandshakeResponse::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << serverVersion;
    ds << screenWidth;
    ds << screenHeight;
    ds << colorDepth;
    ds << supportedFeatures;
    writeFixedStringLE(ds, QString::fromUtf8(serverName), 64);
    writeFixedStringLE(ds, QString::fromUtf8(serverOS), 32);
    return bytes;
}

bool HandshakeResponse::decode(const QByteArray& bytes) {
    if ( bytes.size() < (4 + 2 + 2 + 1 + 1 + 64 + 32) ) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds >> serverVersion;
    ds >> screenWidth;
    ds >> screenHeight;
    ds >> colorDepth;
    ds >> supportedFeatures;
    QString name = readFixedStringLE(ds, 64);
    QString os = readFixedStringLE(ds, 32);
    if ( ds.status() != QDataStream::Ok ) return false;
    memset(serverName, 0, sizeof(serverName));
    memset(serverOS, 0, sizeof(serverOS));
    qstrncpy(serverName, name.toUtf8().constData(), int(sizeof(serverName)) - 1);
    qstrncpy(serverOS, os.toUtf8().constData(), int(sizeof(serverOS)) - 1);
    return true;
}

// AuthenticationRequest 序列化和反序列化实现
QByteArray AuthenticationRequest::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    // username[64]
    writeFixedStringLE(ds, QString::fromUtf8(username), 64);
    // passwordHash[64]
    {
        QByteArray utf8 = QString::fromUtf8(passwordHash).toUtf8();
        if ( utf8.size() > 64 ) utf8.truncate(64);
        // 如果正好64字节，不追加NUL，直接写满64
        if ( !utf8.isEmpty() ) ds.writeRawData(utf8.constData(), utf8.size());
        int pad = 64 - utf8.size();
        if ( pad > 0 ) {
            QByteArray zero(pad, '\0');
            ds.writeRawData(zero.constData(), zero.size());
        }
    }
    // authMethod (quint32)
    ds << static_cast<quint32>(authMethod);
    return bytes;
}

bool AuthenticationRequest::decode(const QByteArray& bytes) {
    if ( bytes.size() < (64 + 64 + 4) ) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    QString user = readFixedStringLE(ds, 64);
    QString pass = readFixedStringLE(ds, 64);
    quint32 method = 0;
    ds >> method;
    if ( ds.status() != QDataStream::Ok ) return false;
    // 填充到旧结构（保留兼容）
    QByteArray u8 = user.toUtf8();
    QByteArray p8 = pass.toUtf8();
    memset(username, 0, sizeof(username));
    memset(passwordHash, 0, sizeof(passwordHash));
    qstrncpy(username, u8.constData(), int(sizeof(username)) - 1);
    // 若正好为64字节（如32字节派生的hex），保留完整64字节，不强制NUL
    if ( p8.size() >= int(sizeof(passwordHash)) ) {
        memcpy(passwordHash, p8.constData(), sizeof(passwordHash));
    } else {
        qstrncpy(passwordHash, p8.constData(), int(sizeof(passwordHash)) - 1);
    }
    authMethod = method;
    return true;
}

// AuthenticationResponse 序列化和反序列化实现
QByteArray AuthenticationResponse::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    // result (quint8)
    ds << static_cast<quint8>(result);
    // sessionId[32]
    writeFixedStringLE(ds, QString::fromUtf8(sessionId), 32);
    // permissions (quint32)
    ds << static_cast<quint32>(permissions);
    return bytes;
}

bool AuthenticationResponse::decode(const QByteArray& bytes) {
    if ( bytes.size() < (1 + 32 + 4) ) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 res8 = 0; ds >> res8;
    QString sid = readFixedStringLE(ds, 32);
    quint32 perms = 0; ds >> perms;
    if ( ds.status() != QDataStream::Ok ) return false;
    result = static_cast<AuthResult>(res8);
    QByteArray sid8 = sid.toUtf8();
    memset(sessionId, 0, sizeof(sessionId));
    qstrncpy(sessionId, sid8.constData(), int(sizeof(sessionId)) - 1);
    permissions = perms;
    return true;
}

// MouseEvent 序列化和反序列化实现
QByteArray MouseEvent::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint8>(eventType);
    ds << static_cast<qint16>(x);
    ds << static_cast<qint16>(y);
    ds << static_cast<qint16>(wheelDelta);
    return bytes;
}

bool MouseEvent::decode(const QByteArray& bytes) {
    if ( bytes.size() < (1 + 2 + 2 + 2) ) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 type8 = 0; qint16 x_val = 0, y_val = 0; qint16 wheel = 0;
    ds >> type8; ds >> x_val; ds >> y_val; ds >> wheel;
    if ( ds.status() != QDataStream::Ok ) return false;
    eventType = static_cast<MouseEventType>(type8);
    x = x_val; y = y_val; wheelDelta = wheel;
    return true;
}

// KeyboardEvent 序列化和反序列化实现
QByteArray KeyboardEvent::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint8>(eventType);
    ds << static_cast<quint32>(keyCode);
    ds << static_cast<quint32>(modifiers);
    // 文本字段定长8字节，UTF-8截断/补零
    writeFixedStringLE(ds, QString::fromUtf8(text), 8);
    return bytes;
}

bool KeyboardEvent::decode(const QByteArray& bytes) {
    if ( bytes.size() < (1 + 4 + 4 + 8) ) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 type8 = 0; quint32 key = 0, mods = 0;
    ds >> type8; ds >> key; ds >> mods;
    QString textStr = readFixedStringLE(ds, 8);
    if ( ds.status() != QDataStream::Ok ) return false;
    eventType = static_cast<KeyboardEventType>(type8);
    keyCode = key; modifiers = mods;
    // 填充C风格数组
    memset(text, 0, sizeof(text));
    QByteArray t8 = textStr.toUtf8();
    qstrncpy(text, t8.constData(), int(sizeof(text)) - 1);
    return true;
}

// FileTransferRequest 序列化和反序列化实现
QByteArray FileTransferRequest::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    // fileName[256]
    writeFixedStringLE(ds, QString::fromUtf8(fileName), 256);
    ds << static_cast<quint64>(fileSize);
    ds << static_cast<quint32>(transferId);
    ds << static_cast<quint8>(direction);
    return bytes;
}

bool FileTransferRequest::decode(const QByteArray& bytes) {
    if ( bytes.size() < (256 + 8 + 4 + 1) ) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    QString name = readFixedStringLE(ds, 256);
    quint64 size = 0; quint32 tid = 0; quint8 dir = 0;
    ds >> size; ds >> tid; ds >> dir;
    if ( ds.status() != QDataStream::Ok ) return false;
    memset(fileName, 0, sizeof(fileName));
    QByteArray n8 = name.toUtf8(); qstrncpy(fileName, n8.constData(), int(sizeof(fileName)) - 1);
    fileSize = size; transferId = tid; direction = dir;
    return true;
}

// FileTransferResponse 序列化和反序列化实现
QByteArray FileTransferResponse::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint32>(transferId);
    ds << static_cast<quint8>(status);
    writeFixedStringLE(ds, QString::fromUtf8(errorMessage), 256);
    return bytes;
}

bool FileTransferResponse::decode(const QByteArray& bytes) {
    if ( bytes.size() < (4 + 1 + 256) ) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 tid = 0; quint8 st = 0;
    ds >> tid; ds >> st;
    QString err = readFixedStringLE(ds, 256);
    if ( ds.status() != QDataStream::Ok ) return false;
    transferId = tid; status = static_cast<FileTransferStatus>(st);
    memset(errorMessage, 0, sizeof(errorMessage));
    QByteArray e8 = err.toUtf8(); qstrncpy(errorMessage, e8.constData(), int(sizeof(errorMessage)) - 1);
    return true;
}

// FileData 序列化和反序列化实现
QByteArray FileData::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint32>(transferId);
    ds << static_cast<quint64>(offset);
    ds << static_cast<quint32>(dataSize);
    // if (!chunk.isEmpty()) ds.writeRawData(chunk.constData(), chunk.size());
    return bytes;
}

bool FileData::decode(const QByteArray& bytes) {
    if ( bytes.size() < (4 + 8 + 4) ) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 tid = 0; quint64 off = 0; quint32 len = 0;
    ds >> tid; ds >> off; ds >> len;
    if ( ds.status() != QDataStream::Ok ) return false;
    // 长度校验
    qsizetype need = qsizetype(4 + 8 + 4) + qsizetype(len);
    if ( bytes.size() < need ) return false;
    // chunkOut = bytes.mid(qsizetype(4 + 8 + 4), len);
    transferId = tid; offset = off; dataSize = len;
    return true;
}

// ScreenData 序列化和反序列化实现
QByteArray ScreenData::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint16>(x);
    ds << static_cast<quint16>(y);
    ds << static_cast<quint16>(width);
    ds << static_cast<quint16>(height);

    // 验证数据大小一致性，防止缓冲区溢出
    quint32 actualDataSize = static_cast<quint32>(imageData.size());
    if ( dataSize != actualDataSize ) {
        qCWarning(lcProtocol, "ScreenData数据大小不一致: dataSize=%u, actual=%u", dataSize, actualDataSize);
        // 使用实际大小以确保数据一致性
        ds << actualDataSize;
    } else {
        ds << static_cast<quint32>(dataSize);
    }

    // 检查数据大小限制，防止内存问题
    const quint32 MAX_SCREEN_DATA_SIZE = 50 * 1024 * 1024; // 50MB限制
    if ( actualDataSize > MAX_SCREEN_DATA_SIZE ) {
        qCWarning(lcProtocol, "ScreenData数据过大: %u bytes，超过限制 %u bytes", actualDataSize, MAX_SCREEN_DATA_SIZE);
        return QByteArray(); // 返回空数据，避免崩溃
    }

    if ( !imageData.isEmpty() ) {
        ds.writeRawData(imageData.constData(), imageData.size());
    }
    return bytes;
}

bool ScreenData::decode(const QByteArray& bytes) {
    // 检查最小头部大小：x(2) + y(2) + width(2) + height(2) + dataSize(4) = 12字节
    const qsizetype headerSize = 2 + 2 + 2 + 2 + 4;
    if ( bytes.size() < headerSize ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol)
            << "ScreenData decode failed: insufficient header size"
            << "- received:" << bytes.size() << "bytes, required:" << headerSize << "bytes";
        return false;
    }

    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);

    quint16 x_val = 0, y_val = 0, w = 0, h = 0;
    quint32 size = 0;

    ds >> x_val;
    ds >> y_val;
    ds >> w;
    ds >> h;
    ds >> size;

    if ( ds.status() != QDataStream::Ok ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol)
            << "ScreenData decode failed: QDataStream error during header parsing"
            << "- stream status:" << ds.status();
        return false;
    }

    // 验证字段合理性
    if ( w == 0 || h == 0 ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol)
            << "ScreenData decode failed: invalid dimensions"
            << "- width:" << w << "height:" << h;
        return false;
    }

    if ( size > 50 * 1024 * 1024 ) { // 50MB 限制
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol)
            << "ScreenData decode failed: image data size too large"
            << "- size:" << size << "bytes (max: 50MB)";
        return false;
    }

    // 检查总大小是否足够包含头部和图像数据
    qsizetype totalNeeded = headerSize + qsizetype(size);
    if ( bytes.size() < totalNeeded ) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol)
            << "ScreenData decode failed: insufficient total size"
            << "- received:" << bytes.size() << "bytes, required:" << totalNeeded << "bytes"
            << "- header size:" << headerSize << "image data size:" << size;
        return false;
    }

    // 赋值解码后的数据
    x = x_val;
    y = y_val;
    width = w;
    height = h;
    dataSize = size;

    // 提取图像数据
    if ( size > 0 ) {
        imageData = bytes.mid(headerSize, size);
        if ( imageData.size() != static_cast<qsizetype>(size) ) {
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol)
                << "ScreenData decode warning: extracted image data size mismatch"
                << "- expected:" << size << "actual:" << imageData.size();

            return false;
        }
    } else {
        imageData = QByteArray();
    }

    return true;
}

// AudioData 序列化和反序列化实现
QByteArray AudioData::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint32>(sampleRate);
    ds << static_cast<quint8>(channels);
    ds << static_cast<quint8>(bitsPerSample);
    ds << static_cast<quint32>(dataSize);
    // if (!pcm.isEmpty()) ds.writeRawData(pcm.constData(), pcm.size());
    return bytes;
}

bool AudioData::decode(const QByteArray& bytes) {
    if ( bytes.size() < (4 + 1 + 1 + 4) ) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 rate = 0, size = 0; quint8 ch = 0, bits = 0;
    ds >> rate; ds >> ch; ds >> bits; ds >> size;
    if ( ds.status() != QDataStream::Ok ) return false;
    qsizetype need = qsizetype(4 + 1 + 1 + 4) + qsizetype(size);
    if ( bytes.size() < need ) return false;
    sampleRate = rate; channels = ch; bitsPerSample = bits; dataSize = size;
    // pcmOut = bytes.mid(qsizetype(4 + 1 + 1 + 4), size);
    return true;
}

/**
 * @brief 序列化 AuthChallenge 结构体
 * @return 序列化后的字节数组
 */
QByteArray AuthChallenge::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);

    ds << method;
    ds << iterations;
    ds << keyLength;

    // 写入盐值的hex字符串（固定64字节）
    ds.writeRawData(saltHex, 64);

    return bytes;
}

/**
 * @brief 反序列化 AuthChallenge 结构体
 * @param bytes 要反序列化的字节数组
 * @return 反序列化是否成功
 */
bool AuthChallenge::decode(const QByteArray& bytes) {
    if ( bytes.size() < (4 + 4 + 4 + 64) ) {
        return false;
    }

    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);

    ds >> method;
    ds >> iterations;
    ds >> keyLength;

    // 读取盐值的hex字符串（固定64字节）
    int bytesRead = ds.readRawData(saltHex, 64);
    if ( bytesRead != 64 || ds.status() != QDataStream::Ok ) {
        return false;
    }

    return true;
}

// CursorPositionMessage 实现
CursorPositionMessage::CursorPositionMessage()
    : cursorType(Qt::ArrowCursor) {
}

CursorPositionMessage::CursorPositionMessage(Qt::CursorShape type)
    : cursorType(type) {
}

QByteArray CursorPositionMessage::encode() const {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<quint8>(cursorType);
    return data;
}

bool CursorPositionMessage::decode(const QByteArray& dataBuffer) {
    if ( dataBuffer.isEmpty() ) {
        return false;
    }

    QDataStream stream(dataBuffer);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint8 type;
    stream >> type;
    cursorType = static_cast<Qt::CursorShape>(type);

    return stream.status() == QDataStream::Ok;
}

// ClipboardMessage 实现
ClipboardMessage::ClipboardMessage()
    : dataType(ClipboardDataType::TEXT), width(0), height(0) {
}

ClipboardMessage::ClipboardMessage(const QString& text)
    : dataType(ClipboardDataType::TEXT), data(text.toUtf8()), width(0), height(0) {
}

ClipboardMessage::ClipboardMessage(const QByteArray& imageData, quint32 w, quint32 h)
    : dataType(ClipboardDataType::IMAGE), data(imageData), width(w), height(h) {
}

bool ClipboardMessage::isText() const {
    return dataType == ClipboardDataType::TEXT;
}

bool ClipboardMessage::isImage() const {
    return dataType == ClipboardDataType::IMAGE;
}

QString ClipboardMessage::text() const {
    return isText() ? QString::fromUtf8(data) : QString();
}

QByteArray ClipboardMessage::imageData() const {
    return isImage() ? data : QByteArray();
}

QByteArray ClipboardMessage::encode() const {
    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    // 写入数据类型
    stream << static_cast<quint8>(dataType);

    if ( dataType == ClipboardDataType::TEXT ) {
        // 编码文本数据
        stream << static_cast<quint32>(data.size());
        stream.writeRawData(data.constData(), data.size());
    } else if ( dataType == ClipboardDataType::IMAGE ) {
        // 编码图片数据
        stream << width;
        stream << height;
        stream << static_cast<quint32>(data.size());
        stream.writeRawData(data.constData(), data.size());
    }

    return buffer;
}

bool ClipboardMessage::decode(const QByteArray& dataBuffer) {
    if ( dataBuffer.size() < static_cast<int>(sizeof(quint8)) ) {
        return false;
    }

    QDataStream stream(dataBuffer);
    stream.setByteOrder(QDataStream::LittleEndian);

    // 读取数据类型
    quint8 type;
    stream >> type;
    dataType = static_cast<ClipboardDataType>(type);

    if ( dataType == ClipboardDataType::TEXT ) {
        // 解码文本数据
        if ( dataBuffer.size() < static_cast<int>(sizeof(quint8) + sizeof(quint32)) ) {
            return false;
        }

        quint32 dataSize;
        stream >> dataSize;

        if ( dataBuffer.size() < static_cast<int>(sizeof(quint8) + sizeof(quint32) + dataSize) ) {
            return false;
        }

        data.resize(dataSize);
        stream.readRawData(data.data(), dataSize);

        // 清空图片相关字段
        width = 0;
        height = 0;

    } else if ( dataType == ClipboardDataType::IMAGE ) {
        // 解码图片数据
        if ( dataBuffer.size() < static_cast<int>(sizeof(quint8) + 3 * sizeof(quint32)) ) {
            return false;
        }

        stream >> width;
        stream >> height;

        quint32 dataSize;
        stream >> dataSize;

        if ( dataBuffer.size() < static_cast<int>(sizeof(quint8) + 3 * sizeof(quint32) + dataSize) ) {
            return false;
        }

        data.resize(dataSize);
        stream.readRawData(data.data(), dataSize);
    } else {
        return false;
    }

    return stream.status() == QDataStream::Ok;
}