// MessageCodec.cpp — 消息结构体序列化/反序列化实现
// 从 ProtocolImpl.cpp 分离，聚焦于 IMessageCodec encode/decode 方法
#include "Protocol.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>
#include <QtCore/QtEndian>

#include "common/config/ProtocolConstants.h"
#include "common/logging/LoggingCategories.h"

// 辅助函数：写入长度前缀字符串（quint32长度 + UTF-8数据）
static void writePrefixedString(QDataStream& ds, const QString& s) {
    QByteArray utf8 = s.toUtf8();
    ds << static_cast<quint32>(utf8.size());
    if (!utf8.isEmpty()) {
        ds.writeRawData(utf8.constData(), utf8.size());
    }
}


// 辅助函数：读取长度前缀字符串
static QString readPrefixedString(QDataStream& ds, quint32 maxLen = ProtocolConstants::MaxGenericStringLength) {
    quint32 len = 0;
    ds >> len;
    if (ds.status() != QDataStream::Ok) return QString();
    if (len > maxLen) {
        // 长度前缀超限 = 数据损坏/协议违规：置错误态使上层 decode 返回 false。
        // 不能只返回空串——那样读指针停在字符串数据前、流状态仍 Ok，
        // 后续字段会把残留数据误当长度前缀解析，导致 decode 对畸形包「假成功」。
        ds.setStatus(QDataStream::ReadCorruptData);
        return QString();
    }
    if (len == 0) return QString();
    QByteArray buf(static_cast<qsizetype>(len), 0);
    int bytesRead = ds.readRawData(buf.data(), static_cast<int>(len));
    if (bytesRead != static_cast<int>(len)) return QString();
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
    if ( data.size() < static_cast<qsizetype>(ProtocolConstants::SerializedHeaderSize) ) {
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
    QByteArray rawData;
    QDataStream ds(&rawData, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    if ( !this->data.isEmpty() ) ds.writeRawData(this->data.constData(), this->data.size());

    return rawData;
}

bool BaseMessage::decode(const QByteArray& rawData) {
    this->data = rawData.mid(qsizetype(0), rawData.size());
    return true;
}

// HandshakeRequest 序列化和反序列化实现
QByteArray HandshakeRequest::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << clientVersion;
    writePrefixedString(ds, clientName);
    writePrefixedString(ds, clientOS);
    return bytes;
}

bool HandshakeRequest::decode(const QByteArray& bytes) {
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds >> clientVersion;
    clientName = readPrefixedString(ds, ProtocolConstants::MaxHostnameLength);
    clientOS = readPrefixedString(ds, ProtocolConstants::MaxHostnameLength);
    return ds.status() == QDataStream::Ok;
}

// HandshakeResponse 序列化和反序列化实现
QByteArray HandshakeResponse::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << serverVersion;
    writePrefixedString(ds, serverName);
    writePrefixedString(ds, serverOS);
    return bytes;
}

bool HandshakeResponse::decode(const QByteArray& bytes) {
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds >> serverVersion;
    serverName = readPrefixedString(ds, ProtocolConstants::MaxHostnameLength);
    serverOS = readPrefixedString(ds, ProtocolConstants::MaxHostnameLength);
    return ds.status() == QDataStream::Ok;
}

// SessionCapabilities 序列化和反序列化实现
QByteArray SessionCapabilities::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << imageQuality;
    ds << colorDepth;
    return bytes;
}

bool SessionCapabilities::decode(const QByteArray& bytes) {
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds >> imageQuality;
    ds >> colorDepth;
    return ds.status() == QDataStream::Ok;
}

// AuthenticationRequest 序列化和反序列化实现
QByteArray AuthenticationRequest::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    writePrefixedString(ds, username);
    writePrefixedString(ds, passwordHash);
    return bytes;
}

bool AuthenticationRequest::decode(const QByteArray& bytes) {
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    username = readPrefixedString(ds, ProtocolConstants::MaxUsernameLength);
    passwordHash = readPrefixedString(ds, ProtocolConstants::MaxPasswordHashLength);
    return ds.status() == QDataStream::Ok;
}

// AuthenticationResponse 序列化和反序列化实现
QByteArray AuthenticationResponse::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint8>(result);
    writePrefixedString(ds, sessionId);
    return bytes;
}

bool AuthenticationResponse::decode(const QByteArray& bytes) {
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 res8 = 0;
    ds >> res8;
    sessionId = readPrefixedString(ds, ProtocolConstants::MaxSessionIdLength);
    if (ds.status() != QDataStream::Ok) return false;
    result = static_cast<AuthResult>(res8);
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
    writePrefixedString(ds, text);
    return bytes;
}

bool KeyboardEvent::decode(const QByteArray& bytes) {
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 type8 = 0; quint32 key = 0, mods = 0;
    ds >> type8; ds >> key; ds >> mods;
    text = readPrefixedString(ds, ProtocolConstants::MaxTextLength);
    if ( ds.status() != QDataStream::Ok ) return false;
    eventType = static_cast<KeyboardEventType>(type8);
    keyCode = key; modifiers = mods;
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
    ds << static_cast<quint16>(originalWidth);
    ds << static_cast<quint16>(originalHeight);

    // 验证数据大小一致性，防止缓冲区溢出
    quint32 actualDataSize = static_cast<quint32>(imageData.size());
    if ( dataSize != actualDataSize ) {
        qCWarning(lcCoreProtocol) << "ScreenData::encode() - Data size mismatch: dataSize=" << dataSize << ", actual=" << actualDataSize;
        // 使用实际大小以确保数据一致性
        ds << actualDataSize;
    } else {
        ds << static_cast<quint32>(dataSize);
    }

    // 写入压缩标志位
    ds << static_cast<quint8>(flags);

    // 写入捕获时间戳（用于客户端端到端延迟测量）
    ds << static_cast<quint64>(captureTimestamp);

    // 检查数据大小限制，防止内存问题
    const quint32 MAX_SCREEN_DATA_SIZE = 50 * 1024 * 1024; // 50MB限制
    if ( actualDataSize > MAX_SCREEN_DATA_SIZE ) {
        qCWarning(lcCoreProtocol) << "ScreenData::encode() - Data too large: " << actualDataSize << " bytes, exceeds limit " << MAX_SCREEN_DATA_SIZE << " bytes";
        return QByteArray(); // 返回空数据，避免崩溃
    }

    if ( !imageData.isEmpty() ) {
        ds.writeRawData(imageData.constData(), imageData.size());
    }
    return bytes;
}

bool ScreenData::decode(const QByteArray& bytes) {
    // 检查最小头部大小：x(2)+y(2)+w(2)+h(2)+origW(2)+origH(2)+dataSize(4)+flags(1)+captureTs(8) = 25 字节
    const qsizetype headerSize = 2 + 2 + 2 + 2 + 2 + 2 + 4 + 1 + 8;
    if ( bytes.size() < headerSize ) {
        qCWarning(lcCoreProtocol)
            << "ScreenData decode failed: insufficient header size"
            << "- received:" << bytes.size() << "bytes, required:" << headerSize << "bytes";
        return false;
    }

    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);

    quint16 x_val = 0, y_val = 0, w = 0, h = 0;
    quint16 origW = 0, origH = 0;
    quint32 size = 0;
    quint8 flagsVal = 0;
    quint64 captureTs = 0;

    ds >> x_val;
    ds >> y_val;
    ds >> w;
    ds >> h;
    ds >> origW;
    ds >> origH;
    ds >> size;
    ds >> flagsVal;
    ds >> captureTs;

    if ( ds.status() != QDataStream::Ok ) {
        qCWarning(lcCoreProtocol)
            << "ScreenData decode failed: QDataStream error during header parsing"
            << "- stream status:" << ds.status();
        return false;
    }

    // 验证字段合理性
    if ( w == 0 || h == 0 ) {
        qCWarning(lcCoreProtocol)
            << "ScreenData decode failed: invalid dimensions"
            << "- width:" << w << "height:" << h;
        return false;
    }

    if ( size > 50 * 1024 * 1024 ) { // 50MB 限制
        qCWarning(lcCoreProtocol)
            << "ScreenData decode failed: image data size too large"
            << "- size:" << size << "bytes (max: 50MB)";
        return false;
    }

    // 检查总大小是否足够包含头部和图像数据
    qsizetype totalNeeded = headerSize + qsizetype(size);
    if ( bytes.size() < totalNeeded ) {
        qCWarning(lcCoreProtocol)
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
    originalWidth = origW;
    originalHeight = origH;
    dataSize = size;
    flags = flagsVal;
    captureTimestamp = captureTs;

    // 提取图像数据
    if ( size > 0 ) {
        imageData = bytes.mid(headerSize, size);
        if ( imageData.size() != static_cast<qsizetype>(size) ) {
            qCWarning(lcCoreProtocol)
                << "ScreenData decode warning: extracted image data size mismatch"
                << "- expected:" << size << "actual:" << imageData.size();

            return false;
        }
    } else {
        imageData = QByteArray();
    }

    return true;
}

// AuthChallenge 序列化和反序列化实现
QByteArray AuthChallenge::encode() const {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << iterations;
    ds << keyLength;
    writePrefixedString(ds, saltHex);
    return bytes;
}

bool AuthChallenge::decode(const QByteArray& bytes) {
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds >> iterations;
    ds >> keyLength;
    saltHex = readPrefixedString(ds, ProtocolConstants::MaxPasswordHashLength);
    return ds.status() == QDataStream::Ok;
}

QByteArray CursorMessage::encode() const {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << posX;
    stream << posY;
    stream << hotX;
    stream << hotY;
    stream << width;
    stream << height;
    stream << qint32(pixels.size());
    if (!pixels.isEmpty()) {
        stream.writeRawData(pixels.constData(), pixels.size());
    }
    return data;
}

bool CursorMessage::decode(const QByteArray& dataBuffer) {
    if (dataBuffer.size() < 28) return false;  // 7 × qint32 最小头

    QDataStream stream(dataBuffer);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream >> posX;
    stream >> posY;
    stream >> hotX;
    stream >> hotY;
    stream >> width;
    stream >> height;
    qint32 pixelSize = 0;
    stream >> pixelSize;

    if (pixelSize > 0) {
        // 用 qsizetype(64 位)计算 28+pixelSize，杜绝 qint32 加法有符号溢出(UB)
        // 回绕为负令边界校验被绕过、畸形光标包假成功
        if (dataBuffer.size() < static_cast<qsizetype>(28) + pixelSize) return false;
        pixels = dataBuffer.mid(28, pixelSize);
    } else {
        pixels.clear();
    }
    return true;
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

        // 用 64 位 qsizetype 比较，杜绝 dataSize 超大时 static_cast<int> 溢出回绕绕过校验
        // （否则畸形包可令 data.resize(dataSize) 分配约 4GiB，远程单包 DoS）
        const qsizetype textRequired = static_cast<qsizetype>(dataSize)
                                     + static_cast<qsizetype>(sizeof(quint8) + sizeof(quint32));
        if ( dataBuffer.size() < textRequired ) {
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

        // 同 TEXT 分支：64 位 qsizetype 比较防止 dataSize 溢出绕过校验
        const qsizetype imageRequired = static_cast<qsizetype>(dataSize)
                                      + static_cast<qsizetype>(sizeof(quint8) + 3 * sizeof(quint32));
        if ( dataBuffer.size() < imageRequired ) {
            return false;
        }

        data.resize(dataSize);
        stream.readRawData(data.data(), dataSize);
    } else {
        return false;
    }

    return stream.status() == QDataStream::Ok;
}