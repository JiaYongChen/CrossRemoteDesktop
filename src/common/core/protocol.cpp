#include "protocol.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include "logging_categories.h"
#include <QtCore/QtEndian>
#include <zlib.h>
#include <QtCore/QMessageLogger>

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
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol) << "无效的魔数:" << Qt::hex << header.magic;
        return false;
    }
    
    // 验证版本
    if (header.version != PROTOCOL_VERSION) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol) << "不支持的协议版本:" << header.version;
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
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol) << "Checksum mismatch. Expected:" << Qt::hex << header.checksum
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
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol) << "Compression failed with error:" << result;
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
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcProtocol) << "Decompression failed with error:" << result;
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

// ---- Stage C: Field-wise serialization implementations ----
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

static QString readFixedStringLE(QDataStream &ds, int fixedLen)
{
    QByteArray buf(fixedLen, 0);
    int read = ds.readRawData(buf.data(), fixedLen);
    if (read != fixedLen) return QString();
    int nul = buf.indexOf('\0');
    if (nul >= 0) buf.truncate(nul);
    return QString::fromUtf8(buf);
}

QByteArray Protocol::encodeAuthenticationRequest(const QString &username,
                                                 const QString &passwordHash,
                                                 quint32 authMethod)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    // username[64]
    writeFixedStringLE(ds, username, 64);
    // passwordHash[64]
    {
        QByteArray utf8 = passwordHash.toUtf8();
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

bool Protocol::decodeAuthenticationRequest(const QByteArray &bytes,
                                           AuthenticationRequest &out)
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
    memset(out.username, 0, sizeof(out.username));
    memset(out.passwordHash, 0, sizeof(out.passwordHash));
    qstrncpy(out.username, u8.constData(), int(sizeof(out.username)) - 1);
    // 若正好为64字节（如32字节派生的hex），保留完整64字节，不强制NUL
    if (p8.size() >= int(sizeof(out.passwordHash))) {
        memcpy(out.passwordHash, p8.constData(), sizeof(out.passwordHash));
    } else {
        qstrncpy(out.passwordHash, p8.constData(), int(sizeof(out.passwordHash)) - 1);
    }
    out.authMethod = method;
    return true;
}

QByteArray Protocol::encodeAuthenticationResponse(AuthResult result,
                                                  const QString &sessionId,
                                                  quint32 permissions)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    // result (quint8)
    ds << static_cast<quint8>(result);
    // sessionId[32]
    writeFixedStringLE(ds, sessionId, 32);
    // permissions (quint32)
    ds << static_cast<quint32>(permissions);
    return bytes;
}

bool Protocol::decodeAuthenticationResponse(const QByteArray &bytes,
                                            AuthenticationResponse &out)
{
    if (bytes.size() < (1 + 32 + 4)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 res8 = 0; ds >> res8;
    QString sid = readFixedStringLE(ds, 32);
    quint32 perms = 0; ds >> perms;
    if (ds.status() != QDataStream::Ok) return false;
    out.result = static_cast<AuthResult>(res8);
    QByteArray sid8 = sid.toUtf8();
    memset(out.sessionId, 0, sizeof(out.sessionId));
    qstrncpy(out.sessionId, sid8.constData(), int(sizeof(out.sessionId)) - 1);
    out.permissions = perms;
    return true;
}

QByteArray Protocol::encodeAuthChallenge(quint32 method,
                                         quint32 iterations,
                                         quint32 keyLength,
                                         const QByteArray &salt)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << method;
    ds << iterations;
    ds << keyLength;
    // 写盐为hex定长域（64字节，超出则截断，不足补零）
    QByteArray saltHex = salt.toHex();
    if (saltHex.size() > 64) saltHex.truncate(64);
    if (!saltHex.isEmpty()) ds.writeRawData(saltHex.constData(), saltHex.size());
    int pad = 64 - saltHex.size();
    if (pad > 0) {
        QByteArray zero(pad, '\0');
        ds.writeRawData(zero.constData(), zero.size());
    }
    return bytes;
}

bool Protocol::decodeAuthChallenge(const QByteArray &bytes, AuthChallenge &out)
{
    if (bytes.size() < (4 + 4 + 4 + 64)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 method=0, iters=0, klen=0;
    ds >> method; ds >> iters; ds >> klen;
    QByteArray saltHexBuf(64, 0);
    int n = ds.readRawData(saltHexBuf.data(), 64);
    if (n != 64 || ds.status() != QDataStream::Ok) return false;
    int nul = saltHexBuf.indexOf('\0');
    if (nul >= 0) saltHexBuf.truncate(nul);
    memset(out.saltHex, 0, sizeof(out.saltHex));
    qstrncpy(out.saltHex, saltHexBuf.constData(), int(sizeof(out.saltHex))-1);
    out.method = method;
    out.iterations = iters;
    out.keyLength = klen;
    return true;
}

QByteArray Protocol::encodeHandshakeRequest(const HandshakeRequest &req)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << req.clientVersion;
    ds << req.screenWidth;
    ds << req.screenHeight;
    ds << req.colorDepth;
    ds << req.compressionLevel;
    writeFixedStringLE(ds, QString::fromUtf8(req.clientName), 64);
    writeFixedStringLE(ds, QString::fromUtf8(req.clientOS), 32);
    return bytes;
}

bool Protocol::decodeHandshakeRequest(const QByteArray &bytes, HandshakeRequest &out)
{
    if (bytes.size() < (4 + 2 + 2 + 1 + 1 + 64 + 32)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds >> out.clientVersion;
    ds >> out.screenWidth;
    ds >> out.screenHeight;
    ds >> out.colorDepth;
    ds >> out.compressionLevel;
    QString name = readFixedStringLE(ds, 64);
    QString os = readFixedStringLE(ds, 32);
    if (ds.status() != QDataStream::Ok) return false;
    memset(out.clientName, 0, sizeof(out.clientName));
    memset(out.clientOS, 0, sizeof(out.clientOS));
    qstrncpy(out.clientName, name.toUtf8().constData(), int(sizeof(out.clientName)) - 1);
    qstrncpy(out.clientOS, os.toUtf8().constData(), int(sizeof(out.clientOS)) - 1);
    return true;
}

QByteArray Protocol::encodeHandshakeResponse(const HandshakeResponse &resp)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << resp.serverVersion;
    ds << resp.screenWidth;
    ds << resp.screenHeight;
    ds << resp.colorDepth;
    ds << resp.supportedFeatures;
    writeFixedStringLE(ds, QString::fromUtf8(resp.serverName), 64);
    writeFixedStringLE(ds, QString::fromUtf8(resp.serverOS), 32);
    return bytes;
}

bool Protocol::decodeHandshakeResponse(const QByteArray &bytes, HandshakeResponse &out)
{
    if (bytes.size() < (4 + 2 + 2 + 1 + 1 + 64 + 32)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds >> out.serverVersion;
    ds >> out.screenWidth;
    ds >> out.screenHeight;
    ds >> out.colorDepth;
    ds >> out.supportedFeatures;
    QString name = readFixedStringLE(ds, 64);
    QString os = readFixedStringLE(ds, 32);
    if (ds.status() != QDataStream::Ok) return false;
    memset(out.serverName, 0, sizeof(out.serverName));
    memset(out.serverOS, 0, sizeof(out.serverOS));
    qstrncpy(out.serverName, name.toUtf8().constData(), int(sizeof(out.serverName)) - 1);
    qstrncpy(out.serverOS, os.toUtf8().constData(), int(sizeof(out.serverOS)) - 1);
    return true;
}

QByteArray Protocol::encodeMouseEvent(const MouseEvent &ev)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint8>(ev.eventType);
    ds << static_cast<qint16>(ev.x);
    ds << static_cast<qint16>(ev.y);
    ds << static_cast<quint8>(ev.buttons);
    ds << static_cast<qint16>(ev.wheelDelta);
    return bytes;
}

bool Protocol::decodeMouseEvent(const QByteArray &bytes, MouseEvent &out)
{
    if (bytes.size() < (1 + 2 + 2 + 1 + 2)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 type8 = 0; qint16 x=0,y=0; quint8 btn=0; qint16 wheel=0;
    ds >> type8; ds >> x; ds >> y; ds >> btn; ds >> wheel;
    if (ds.status() != QDataStream::Ok) return false;
    out.eventType = static_cast<MouseEventType>(type8);
    out.x = x; out.y = y; out.buttons = btn; out.wheelDelta = wheel;
    return true;
}

QByteArray Protocol::encodeKeyboardEvent(const KeyboardEvent &ev)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint8>(ev.eventType);
    ds << static_cast<quint32>(ev.keyCode);
    ds << static_cast<quint32>(ev.modifiers);
    // 文本字段定长8字节，UTF-8截断/补零
    writeFixedStringLE(ds, QString::fromUtf8(ev.text), 8);
    return bytes;
}

bool Protocol::decodeKeyboardEvent(const QByteArray &bytes, KeyboardEvent &out)
{
    if (bytes.size() < (1 + 4 + 4 + 8)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 type8=0; quint32 key=0, mods=0; 
    ds >> type8; ds >> key; ds >> mods;
    QString text = readFixedStringLE(ds, 8);
    if (ds.status() != QDataStream::Ok) return false;
    out.eventType = static_cast<KeyboardEventType>(type8);
    out.keyCode = key; out.modifiers = mods;
    // 填充C风格数组
    memset(out.text, 0, sizeof(out.text));
    QByteArray t8 = text.toUtf8();
    qstrncpy(out.text, t8.constData(), int(sizeof(out.text)) - 1);
    return true;
}

QByteArray Protocol::encodeErrorMessage(quint32 errorCode, const QString &errorText)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint32>(errorCode);
    writeFixedStringLE(ds, errorText, 256);
    return bytes;
}

bool Protocol::decodeErrorMessage(const QByteArray &bytes, ErrorMessage &out)
{
    if (bytes.size() < (4 + 256)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 code=0; ds >> code;
    QString text = readFixedStringLE(ds, 256);
    if (ds.status() != QDataStream::Ok) return false;
    out.errorCode = code;
    memset(out.errorText, 0, sizeof(out.errorText));
    QByteArray t8 = text.toUtf8();
    qstrncpy(out.errorText, t8.constData(), int(sizeof(out.errorText)) - 1);
    return true;
}

QByteArray Protocol::encodeStatusUpdate(const StatusUpdate &st)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint8>(st.connectionStatus);
    ds << static_cast<quint32>(st.bytesReceived);
    ds << static_cast<quint32>(st.bytesSent);
    ds << static_cast<quint16>(st.fps);
    ds << static_cast<quint8>(st.cpuUsage);
    ds << static_cast<quint32>(st.memoryUsage);
    return bytes;
}

bool Protocol::decodeStatusUpdate(const QByteArray &bytes, StatusUpdate &out)
{
    if (bytes.size() < (1 + 4 + 4 + 2 + 1 + 4)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 cs=0; quint32 br=0, bs=0; quint16 fps=0; quint8 cpu=0; quint32 mem=0;
    ds >> cs; ds >> br; ds >> bs; ds >> fps; ds >> cpu; ds >> mem;
    if (ds.status() != QDataStream::Ok) return false;
    out.connectionStatus = cs;
    out.bytesReceived = br;
    out.bytesSent = bs;
    out.fps = fps;
    out.cpuUsage = cpu;
    out.memoryUsage = mem;
    return true;
}

QByteArray Protocol::encodeFileTransferRequest(const FileTransferRequest &req)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    // fileName[256]
    writeFixedStringLE(ds, QString::fromUtf8(req.fileName), 256);
    ds << static_cast<quint64>(req.fileSize);
    ds << static_cast<quint32>(req.transferId);
    ds << static_cast<quint8>(req.direction);
    return bytes;
}

bool Protocol::decodeFileTransferRequest(const QByteArray &bytes, FileTransferRequest &out)
{
    if (bytes.size() < (256 + 8 + 4 + 1)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    QString name = readFixedStringLE(ds, 256);
    quint64 size=0; quint32 tid=0; quint8 dir=0;
    ds >> size; ds >> tid; ds >> dir;
    if (ds.status() != QDataStream::Ok) return false;
    memset(out.fileName, 0, sizeof(out.fileName));
    QByteArray n8 = name.toUtf8(); qstrncpy(out.fileName, n8.constData(), int(sizeof(out.fileName)) - 1);
    out.fileSize = size; out.transferId = tid; out.direction = dir;
    return true;
}

QByteArray Protocol::encodeFileTransferResponse(const FileTransferResponse &resp)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint32>(resp.transferId);
    ds << static_cast<quint8>(resp.status);
    writeFixedStringLE(ds, QString::fromUtf8(resp.errorMessage), 256);
    return bytes;
}

bool Protocol::decodeFileTransferResponse(const QByteArray &bytes, FileTransferResponse &out)
{
    if (bytes.size() < (4 + 1 + 256)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint32 tid=0; quint8 st=0; 
    ds >> tid; ds >> st;
    QString err = readFixedStringLE(ds, 256);
    if (ds.status() != QDataStream::Ok) return false;
    out.transferId = tid; out.status = static_cast<FileTransferStatus>(st);
    memset(out.errorMessage, 0, sizeof(out.errorMessage));
    QByteArray e8 = err.toUtf8(); qstrncpy(out.errorMessage, e8.constData(), int(sizeof(out.errorMessage)) - 1);
    return true;
}

QByteArray Protocol::encodeFileData(const FileData &hdr, const QByteArray &data)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint32>(hdr.transferId);
    ds << static_cast<quint64>(hdr.offset);
    ds << static_cast<quint32>(hdr.dataSize);
    if (!data.isEmpty()) ds.writeRawData(data.constData(), data.size());
    return bytes;
}

bool Protocol::decodeFileData(const QByteArray &bytes, FileData &hdrOut, QByteArray &dataOut)
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
    dataOut = bytes.mid(qsizetype(4 + 8 + 4), len);
    hdrOut.transferId = tid; hdrOut.offset = off; hdrOut.dataSize = len;
    return true;
}

QByteArray Protocol::encodeClipboardData(quint8 dataType, const QByteArray &data)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<quint8>(dataType);
    ds << static_cast<quint32>(data.size());
    if (!data.isEmpty()) ds.writeRawData(data.constData(), data.size());
    return bytes;
}

bool Protocol::decodeClipboardData(const QByteArray &bytes, ClipboardData &metaOut, QByteArray &dataOut)
{
    if (bytes.size() < (1 + 4)) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 t=0; quint32 len=0;
    ds >> t; ds >> len;
    if (ds.status() != QDataStream::Ok) return false;
    qsizetype need = qsizetype(1 + 4) + qsizetype(len);
    if (bytes.size() < need) return false;
    metaOut.dataType = t; metaOut.dataSize = len;
    dataOut = bytes.mid(qsizetype(1 + 4), len);
    return true;
}