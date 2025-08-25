#include "protocol.h"
#include <QtCore/QDataStream>
#include <QtCore/QIODevice>
#include <QtCore/QString>
#include <QtCore/QDebug>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QtEndian>
#include "../core/logging_categories.h"
#include <QtCore/QMessageLogger>
#include <cstring>

// 辅助函数：写入定长字符串（小端）
static void writeFixedStringLE(QDataStream &ds, const QString &s, int fixedLen)
{
    QByteArray utf8 = s.toUtf8();
    if (utf8.size() > fixedLen) utf8.truncate(fixedLen);
    // 写入定长字节，未满补零
    if (!utf8.isEmpty()) ds.writeRawData(utf8.constData(), utf8.size());
    int pad = fixedLen - utf8.size();
    if (pad > 0) {
        QByteArray zero(pad, '\0');
        ds.writeRawData(zero.constData(), zero.size());
    }
}

// 辅助函数：读取定长字符串（小端）
static QString readFixedStringLE(QDataStream &ds, int fixedLen)
{
    QByteArray buf(fixedLen, 0);
    int read = ds.readRawData(buf.data(), fixedLen);
    if (read != fixedLen) return QString();
    int nul = buf.indexOf('\0');
    if (nul >= 0) buf.truncate(nul);
    return QString::fromUtf8(buf);
}

// MessageHeader 序列化和反序列化实现
QByteArray MessageHeader::encode() const
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << "MessageHeader:" << magic << version << static_cast<quint32>(type) << length << checksum << timestamp;
    
    stream << magic;
    stream << version;
    stream << static_cast<quint32>(type);
    stream << length;
    stream << checksum;
    stream << timestamp;
    
    return data;
}

bool MessageHeader::decode(const QByteArray &data)
{
    if (data.size() < static_cast<qsizetype>(SERIALIZED_HEADER_SIZE)) {
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

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcClient) << "MessageHeader decode:" << magic << version << static_cast<quint32>(type) << length << checksum << timestamp;
    
    // 验证魔数
    if (magic != PROTOCOL_MAGIC) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol) << "无效的魔数:" << Qt::hex << magic;
        return false;
    }
    
    // 验证版本
    if (version != PROTOCOL_VERSION) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol) << "不支持的协议版本:" << version;
        return false;
    }
    
    return true;
}

QByteArray BaseMessage::encode() const
{
    QByteArray data;
    QDataStream ds(&data, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    if (!this->data.isEmpty()) ds.writeRawData(this->data.constData(), this->data.size());
    
    return data;
}

bool BaseMessage::decode(const QByteArray &data)
{
    this->data = data.mid(qsizetype(0), data.size());
    return true;
}

// HandshakeRequest 序列化和反序列化实现
QByteArray HandshakeRequest::encode() const
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << clientVersion;
    ds << screenWidth;
    ds << screenHeight;
    ds << colorDepth;
    ds << compressionLevel;
    writeFixedStringLE(ds, QString::fromUtf8(clientName), 64);
    writeFixedStringLE(ds, QString::fromUtf8(clientOS), 32);
    return bytes;
}

bool HandshakeRequest::decode(const QByteArray &bytes)
{
    if (bytes.size() < (4 + 2 + 2 + 1 + 1 + 64 + 32)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds >> clientVersion;
    ds >> screenWidth;
    ds >> screenHeight;
    ds >> colorDepth;
    ds >> compressionLevel;
    QString name = readFixedStringLE(ds, 64);
    QString os = readFixedStringLE(ds, 32);
    if (ds.status() != QDataStream::Ok) return false;
    memset(clientName, 0, sizeof(clientName));
    memset(clientOS, 0, sizeof(clientOS));
    qstrncpy(clientName, name.toUtf8().constData(), int(sizeof(clientName)) - 1);
    qstrncpy(clientOS, os.toUtf8().constData(), int(sizeof(clientOS)) - 1);
    return true;
}

// HandshakeResponse 序列化和反序列化实现
QByteArray HandshakeResponse::encode() const
{
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

bool HandshakeResponse::decode(const QByteArray &bytes)
{
    if (bytes.size() < (4 + 2 + 2 + 1 + 1 + 64 + 32)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds >> serverVersion;
    ds >> screenWidth;
    ds >> screenHeight;
    ds >> colorDepth;
    ds >> supportedFeatures;
    QString name = readFixedStringLE(ds, 64);
    QString os = readFixedStringLE(ds, 32);
    if (ds.status() != QDataStream::Ok) return false;
    memset(serverName, 0, sizeof(serverName));
    memset(serverOS, 0, sizeof(serverOS));
    qstrncpy(serverName, name.toUtf8().constData(), int(sizeof(serverName)) - 1);
    qstrncpy(serverOS, os.toUtf8().constData(), int(sizeof(serverOS)) - 1);
    return true;
}

// AuthenticationRequest 序列化和反序列化实现
QByteArray AuthenticationRequest::encode() const
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    // username[64]
    writeFixedStringLE(ds, QString::fromUtf8(username), 64);
    // passwordHash[64]
    {
        QByteArray utf8 = QString::fromUtf8(passwordHash).toUtf8();
        if (utf8.size() > 64) utf8.truncate(64);
        // 如果正好64字节，不追加NUL，直接写满64
        if (!utf8.isEmpty()) ds.writeRawData(utf8.constData(), utf8.size());
        int pad = 64 - utf8.size();
        if (pad > 0) {
            QByteArray zero(pad, '\0');
            ds.writeRawData(zero.constData(), zero.size());
        }
    }
    // authMethod (quint32)
    ds << static_cast<quint32>(authMethod);
    return bytes;
}

bool AuthenticationRequest::decode(const QByteArray &bytes)
{
    if (bytes.size() < (64 + 64 + 4)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    QString user = readFixedStringLE(ds, 64);
    QString pass = readFixedStringLE(ds, 64);
    quint32 method = 0;
    ds >> method;
    if (ds.status() != QDataStream::Ok) return false;
    // 填充到旧结构（保留兼容）
    QByteArray u8 = user.toUtf8();
    QByteArray p8 = pass.toUtf8();
    memset(username, 0, sizeof(username));
    memset(passwordHash, 0, sizeof(passwordHash));
    qstrncpy(username, u8.constData(), int(sizeof(username)) - 1);
    // 若正好为64字节（如32字节派生的hex），保留完整64字节，不强制NUL
    if (p8.size() >= int(sizeof(passwordHash))) {
        memcpy(passwordHash, p8.constData(), sizeof(passwordHash));
    } else {
        qstrncpy(passwordHash, p8.constData(), int(sizeof(passwordHash)) - 1);
    }
    authMethod = method;
    return true;
}

// AuthenticationResponse 序列化和反序列化实现
QByteArray AuthenticationResponse::encode() const
{
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

bool AuthenticationResponse::decode(const QByteArray &bytes)
{
    if (bytes.size() < (1 + 32 + 4)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 res8 = 0; ds >> res8;
    QString sid = readFixedStringLE(ds, 32);
    quint32 perms = 0; ds >> perms;
    if (ds.status() != QDataStream::Ok) return false;
    result = static_cast<AuthResult>(res8);
    QByteArray sid8 = sid.toUtf8();
    memset(sessionId, 0, sizeof(sessionId));
    qstrncpy(sessionId, sid8.constData(), int(sizeof(sessionId)) - 1);
    permissions = perms;
    return true;
}

// MouseEvent 序列化和反序列化实现
QByteArray MouseEvent::encode() const
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint8>(eventType);
    ds << static_cast<qint16>(x);
    ds << static_cast<qint16>(y);
    ds << static_cast<quint8>(buttons);
    ds << static_cast<qint16>(wheelDelta);
    return bytes;
}

bool MouseEvent::decode(const QByteArray &bytes)
{
    if (bytes.size() < (1 + 2 + 2 + 1 + 2)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 type8 = 0; qint16 x_val=0, y_val=0; quint8 btn=0; qint16 wheel=0;
    ds >> type8; ds >> x_val; ds >> y_val; ds >> btn; ds >> wheel;
    if (ds.status() != QDataStream::Ok) return false;
    eventType = static_cast<MouseEventType>(type8);
    x = x_val; y = y_val; buttons = btn; wheelDelta = wheel;
    return true;
}

// KeyboardEvent 序列化和反序列化实现
QByteArray KeyboardEvent::encode() const
{
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

bool KeyboardEvent::decode(const QByteArray &bytes)
{
    if (bytes.size() < (1 + 4 + 4 + 8)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 type8=0; quint32 key=0, mods=0; 
    ds >> type8; ds >> key; ds >> mods;
    QString textStr = readFixedStringLE(ds, 8);
    if (ds.status() != QDataStream::Ok) return false;
    eventType = static_cast<KeyboardEventType>(type8);
    keyCode = key; modifiers = mods;
    // 填充C风格数组
    memset(text, 0, sizeof(text));
    QByteArray t8 = textStr.toUtf8();
    qstrncpy(text, t8.constData(), int(sizeof(text)) - 1);
    return true;
}

// ErrorMessage 序列化和反序列化实现
QByteArray ErrorMessage::encode() const
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint32>(errorCode);
    writeFixedStringLE(ds, QString::fromUtf8(errorText), 256);
    return bytes;
}

bool ErrorMessage::decode(const QByteArray &bytes)
{
    if (bytes.size() < (4 + 256)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 code=0; ds >> code;
    QString textStr = readFixedStringLE(ds, 256);
    if (ds.status() != QDataStream::Ok) return false;
    errorCode = code;
    memset(errorText, 0, sizeof(errorText));
    QByteArray t8 = textStr.toUtf8();
    qstrncpy(errorText, t8.constData(), int(sizeof(errorText)) - 1);
    return true;
}

// StatusUpdate 序列化和反序列化实现
QByteArray StatusUpdate::encode() const
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint8>(connectionStatus);
    ds << static_cast<quint32>(bytesReceived);
    ds << static_cast<quint32>(bytesSent);
    ds << static_cast<quint16>(fps);
    ds << static_cast<quint8>(cpuUsage);
    ds << static_cast<quint32>(memoryUsage);
    return bytes;
}

bool StatusUpdate::decode(const QByteArray &bytes)
{
    if (bytes.size() < (1 + 4 + 4 + 2 + 1 + 4)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 cs=0; quint32 br=0, bs=0; quint16 fpsVal=0; quint8 cpu=0; quint32 mem=0;
    ds >> cs; ds >> br; ds >> bs; ds >> fpsVal; ds >> cpu; ds >> mem;
    if (ds.status() != QDataStream::Ok) return false;
    connectionStatus = cs;
    bytesReceived = br;
    bytesSent = bs;
    fps = fpsVal;
    cpuUsage = cpu;
    memoryUsage = mem;
    return true;
}

// FileTransferRequest 序列化和反序列化实现
QByteArray FileTransferRequest::encode() const
{
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

bool FileTransferRequest::decode(const QByteArray &bytes)
{
    if (bytes.size() < (256 + 8 + 4 + 1)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    QString name = readFixedStringLE(ds, 256);
    quint64 size=0; quint32 tid=0; quint8 dir=0;
    ds >> size; ds >> tid; ds >> dir;
    if (ds.status() != QDataStream::Ok) return false;
    memset(fileName, 0, sizeof(fileName));
    QByteArray n8 = name.toUtf8(); qstrncpy(fileName, n8.constData(), int(sizeof(fileName)) - 1);
    fileSize = size; transferId = tid; direction = dir;
    return true;
}

// FileTransferResponse 序列化和反序列化实现
QByteArray FileTransferResponse::encode() const
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint32>(transferId);
    ds << static_cast<quint8>(status);
    writeFixedStringLE(ds, QString::fromUtf8(errorMessage), 256);
    return bytes;
}

bool FileTransferResponse::decode(const QByteArray &bytes)
{
    if (bytes.size() < (4 + 1 + 256)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 tid=0; quint8 st=0; 
    ds >> tid; ds >> st;
    QString err = readFixedStringLE(ds, 256);
    if (ds.status() != QDataStream::Ok) return false;
    transferId = tid; status = static_cast<FileTransferStatus>(st);
    memset(errorMessage, 0, sizeof(errorMessage));
    QByteArray e8 = err.toUtf8(); qstrncpy(errorMessage, e8.constData(), int(sizeof(errorMessage)) - 1);
    return true;
}

// FileData 序列化和反序列化实现
QByteArray FileData::encode() const
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint32>(transferId);
    ds << static_cast<quint64>(offset);
    ds << static_cast<quint32>(dataSize);
    // if (!chunk.isEmpty()) ds.writeRawData(chunk.constData(), chunk.size());
    return bytes;
}

bool FileData::decode(const QByteArray &bytes)
{
    if (bytes.size() < (4 + 8 + 4)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 tid=0; quint64 off=0; quint32 len=0;
    ds >> tid; ds >> off; ds >> len;
    if (ds.status() != QDataStream::Ok) return false;
    // 长度校验
    qsizetype need = qsizetype(4 + 8 + 4) + qsizetype(len);
    if (bytes.size() < need) return false;
    // chunkOut = bytes.mid(qsizetype(4 + 8 + 4), len);
    transferId = tid; offset = off; dataSize = len;
    return true;
}

// ClipboardData 序列化和反序列化实现
QByteArray ClipboardData::encode() const
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint8>(dataType);
    ds << static_cast<quint32>(dataSize);
    // if (!payload.isEmpty()) ds.writeRawData(payload.constData(), payload.size());
    return bytes;
}

bool ClipboardData::decode(const QByteArray &bytes)
{
    if (bytes.size() < (1 + 4)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 t=0; quint32 len=0;
    ds >> t; ds >> len;
    if (ds.status() != QDataStream::Ok) return false;
    qsizetype need = qsizetype(1 + 4) + qsizetype(len);
    if (bytes.size() < need) return false;
    dataType = t; dataSize = len;
    // payloadOut = bytes.mid(qsizetype(1 + 4), len);
    return true;
}

// ScreenData 序列化和反序列化实现
QByteArray ScreenData::encode() const
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint16>(x);
    ds << static_cast<quint16>(y);
    ds << static_cast<quint16>(width);
    ds << static_cast<quint16>(height);
    ds << static_cast<quint8>(imageType);
    ds << static_cast<quint8>(compressionType);
    ds << static_cast<quint32>(dataSize);
    if (!imageData.isEmpty()) ds.writeRawData(imageData.constData(), imageData.size());
    return bytes;
}

bool ScreenData::decode(const QByteArray &bytes)
{
    // 检查最小头部大小：x(2) + y(2) + width(2) + height(2) + imageType(1) + compressionType(1) + dataSize(4) = 14字节
    const qsizetype headerSize = 2 + 2 + 2 + 2 + 1 + 1 + 4;
    if (bytes.size() < headerSize) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol)
            << "ScreenData decode failed: insufficient header size"
            << "- received:" << bytes.size() << "bytes, required:" << headerSize << "bytes";
        return false;
    }
    
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    
    quint16 x_val=0, y_val=0, w=0, h=0; 
    quint8 imty=0, comp=0; 
    quint32 size=0;
    
    ds >> x_val; 
    ds >> y_val; 
    ds >> w; 
    ds >> h; 
    ds >> imty; 
    ds >> comp; 
    ds >> size;
    
    if (ds.status() != QDataStream::Ok) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol)
            << "ScreenData decode failed: QDataStream error during header parsing"
            << "- stream status:" << ds.status();
        return false;
    }
    
    // 验证字段合理性
    if (w == 0 || h == 0) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol)
            << "ScreenData decode failed: invalid dimensions"
            << "- width:" << w << "height:" << h;
        return false;
    }
    
    if (size > 50 * 1024 * 1024) { // 50MB 限制
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol)
            << "ScreenData decode failed: image data size too large"
            << "- size:" << size << "bytes (max: 50MB)";
        return false;
    }
    
    // 检查总大小是否足够包含头部和图像数据
    qsizetype totalNeeded = headerSize + qsizetype(size);
    if (bytes.size() < totalNeeded) {
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
    imageType = imty; 
    compressionType = comp; 
    dataSize = size;
    
    // 提取图像数据
    if (size > 0) {
        imageData = bytes.mid(headerSize, size);
        if (imageData.size() != static_cast<qsizetype>(size)) {
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol)
                << "ScreenData decode warning: extracted image data size mismatch"
                << "- expected:" << size << "actual:" << imageData.size();
        }
    } else {
        imageData = QByteArray();
    }
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcProtocol)
        << "ScreenData decode successful"
        << "- position:" << x_val << "," << y_val
        << "- dimensions:" << w << "x" << h
        << "- image type:" << imty << "compression:" << comp
        << "- data size:" << size;
    
    return true;
}

// AudioData 序列化和反序列化实现
QByteArray AudioData::encode() const
{
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

bool AudioData::decode(const QByteArray &bytes)
{
    if (bytes.size() < (4 + 1 + 1 + 4)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 rate=0, size=0; quint8 ch=0, bits=0;
    ds >> rate; ds >> ch; ds >> bits; ds >> size;
    if (ds.status() != QDataStream::Ok) return false;
    qsizetype need = qsizetype(4 + 1 + 1 + 4) + qsizetype(size);
    if (bytes.size() < need) return false;
    sampleRate = rate; channels = ch; bitsPerSample = bits; dataSize = size;
    // pcmOut = bytes.mid(qsizetype(4 + 1 + 1 + 4), size);
    return true;
}

/**
 * @brief 序列化 AuthChallenge 结构体
 * @return 序列化后的字节数组
 */
QByteArray AuthChallenge::encode() const
{
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
bool AuthChallenge::decode(const QByteArray &bytes)
{
    if (bytes.size() < (4 + 4 + 4 + 64)) {
        return false;
    }
    
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    
    ds >> method;
    ds >> iterations;
    ds >> keyLength;
    
    // 读取盐值的hex字符串（固定64字节）
    int bytesRead = ds.readRawData(saltHex, 64);
    if (bytesRead != 64 || ds.status() != QDataStream::Ok) {
        return false;
    }
    
    return true;
}