#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QtCore/QByteArray>
#include <QtCore/QDataStream>
#include <QtCore/QIODevice>
#include <QtCore/qglobal.h>

// 协议版本
#define PROTOCOL_VERSION 1

// 魔数用于验证数据包
#define PROTOCOL_MAGIC 0x52444350  // "RDCP" in hex

// 序列化后的消息头大小：6个quint32字段 + 1个quint64字段 = 32字节
#define SERIALIZED_HEADER_SIZE (6 * sizeof(quint32) + sizeof(quint64))

// 消息类型枚举
enum class MessageType : quint32 {
    // 连接管理
    HANDSHAKE_REQUEST = 0x0001,
    HANDSHAKE_RESPONSE = 0x0002,
    AUTHENTICATION_REQUEST = 0x0003,
    AUTHENTICATION_RESPONSE = 0x0004,
    DISCONNECT_REQUEST = 0x0005,
    HEARTBEAT = 0x0006,
    AUTH_CHALLENGE = 0x0007,
    
    // 屏幕数据
    SCREEN_DATA = 0x1001,
    SCREEN_UPDATE = 0x1002,
    SCREEN_RESOLUTION = 0x1003,
    CURSOR_POSITION = 0x1004,
    CURSOR_SHAPE = 0x1005,
    
    // 输入事件
    MOUSE_EVENT = 0x2001,
    KEYBOARD_EVENT = 0x2002,
    
    // 音频数据
    AUDIO_DATA = 0x3001,
    AUDIO_FORMAT = 0x3002,
    
    // 文件传输
    FILE_TRANSFER_REQUEST = 0x4001,
    FILE_TRANSFER_RESPONSE = 0x4002,
    FILE_DATA = 0x4003,
    FILE_TRANSFER_COMPLETE = 0x4004,
    FILE_TRANSFER_ERROR = 0x4005,
    
    // 剪贴板
    CLIPBOARD_DATA = 0x5001,
    
    // 错误和状态
    ERROR_MESSAGE = 0x9001,
    STATUS_UPDATE = 0x9002
};

// 鼠标事件类型
enum class MouseEventType : quint8 {
    MOVE = 0x01,
    LEFT_PRESS = 0x02,
    LEFT_RELEASE = 0x03,
    RIGHT_PRESS = 0x04,
    RIGHT_RELEASE = 0x05,
    MIDDLE_PRESS = 0x06,
    MIDDLE_RELEASE = 0x07,
    WHEEL_UP = 0x08,
    WHEEL_DOWN = 0x09
};

// 键盘事件类型
enum class KeyboardEventType : quint8 {
    KEY_PRESS = 0x01,
    KEY_RELEASE = 0x02
};

// 认证结果
enum class AuthResult : quint8 {
    SUCCESS = 0x00,
    INVALID_PASSWORD = 0x01,
    ACCESS_DENIED = 0x02,
    SERVER_FULL = 0x03,
    UNKNOWN_ERROR = 0xFF
};

// 文件传输状态
enum class FileTransferStatus : quint8 {
    PENDING = 0x00,
    IN_PROGRESS = 0x01,
    COMPLETED = 0x02,
    CANCELLED = 0x03,
    ERROR = 0x04
};

// 消息头结构
struct MessageHeader {
    quint32 magic;          // 魔数
    quint32 version;        // 协议版本
    MessageType type;       // 消息类型
    quint32 length;         // 数据长度
    quint32 sequence;       // 序列号
    quint32 checksum;       // 校验和
    quint64 timestamp;      // 时间戳
    
    MessageHeader()
        : magic(PROTOCOL_MAGIC)
        , version(PROTOCOL_VERSION)
        , type(MessageType::HEARTBEAT)
        , length(0)
        , sequence(0)
        , checksum(0)
        , timestamp(0)
    {}
};

// 握手请求数据
struct HandshakeRequest {
    quint32 clientVersion;
    quint16 screenWidth;
    quint16 screenHeight;
    quint8 colorDepth;
    quint8 compressionLevel;
    char clientName[64];
    char clientOS[32];
};

// 握手响应数据
struct HandshakeResponse {
    quint32 serverVersion;
    quint16 screenWidth;
    quint16 screenHeight;
    quint8 colorDepth;
    quint8 supportedFeatures;
    char serverName[64];
    char serverOS[32];
};

// 认证请求数据
struct AuthenticationRequest {
    char username[64];
    char passwordHash[64];
    quint32 authMethod;
};

// 认证响应数据
struct AuthenticationResponse {
    AuthResult result;
    char sessionId[32];
    quint32 permissions;
};

// 认证挑战（阶段C：PBKDF2 握手参数）
struct AuthChallenge {
    quint32 method;      // 1=PBKDF2_SHA256（约定）
    quint32 iterations;  // 推荐 100000
    quint32 keyLength;   // 派生长度（字节），如 32
    char    saltHex[64]; // 盐（hex字符串，最多32字节盐=64字符）
};

// 鼠标事件数据
struct MouseEvent {
    MouseEventType eventType;
    qint16 x;
    qint16 y;
    quint8 buttons;
    qint16 wheelDelta;
};

// 键盘事件数据
struct KeyboardEvent {
    KeyboardEventType eventType;
    quint32 keyCode;
    quint32 modifiers;
    char text[8];
};

// 屏幕数据
struct ScreenData {
    quint16 x;
    quint16 y;
    quint16 width;
    quint16 height;
    quint8 compressionType;
    quint32 dataSize;
    // 实际图像数据跟在后面
};

// 音频数据
struct AudioData {
    quint32 sampleRate;
    quint8 channels;
    quint8 bitsPerSample;
    quint32 dataSize;
    // 实际音频数据跟在后面
};

// 文件传输请求
struct FileTransferRequest {
    char fileName[256];
    quint64 fileSize;
    quint32 transferId;
    quint8 direction; // 0: upload, 1: download
};

// 文件传输响应
struct FileTransferResponse {
    quint32 transferId;
    FileTransferStatus status;
    char errorMessage[256];
};

// 文件数据块
struct FileData {
    quint32 transferId;
    quint64 offset;
    quint32 dataSize;
    // 实际文件数据跟在后面
};

// 剪贴板数据
struct ClipboardData {
    quint8 dataType; // 0: text, 1: image
    quint32 dataSize;
    // 实际剪贴板数据跟在后面
};

// 错误消息
struct ErrorMessage {
    quint32 errorCode;
    char errorText[256];
};

// 状态更新
struct StatusUpdate {
    quint8 connectionStatus;
    quint32 bytesReceived;
    quint32 bytesSent;
    quint16 fps;
    quint8 cpuUsage;
    quint32 memoryUsage;
};

// 协议工具类
class Protocol
{
public:
    // 计算校验和
    static quint32 calculateChecksum(const QByteArray &data);
    
    // 序列化消息头
    static QByteArray serializeHeader(const MessageHeader &header);
    
    // 反序列化消息头
    static bool deserializeHeader(const QByteArray &data, MessageHeader &header);
    
    // 创建消息
    static QByteArray createMessage(MessageType type, const QByteArray &payload = QByteArray());
    
    // 解析消息
    static bool parseMessage(const QByteArray &data, MessageHeader &header, QByteArray &payload);
    
    // 验证消息
    static bool validateMessage(const MessageHeader &header, const QByteArray &payload);
    
    // 压缩数据
    static QByteArray compressData(const QByteArray &data, int level = 6);
    
    // 解压数据
    static QByteArray decompressData(const QByteArray &data);
    
    // 加密数据
    static QByteArray encryptData(const QByteArray &data, const QByteArray &key);
    
    // 解密数据
    static QByteArray decryptData(const QByteArray &data, const QByteArray &key);

    // ---- Stage C: Field-wise serialization (no reinterpret_cast) ----
    // AuthenticationRequest 编码/解码（按字段小端写入，定长区填零补齐/截断）
    static QByteArray encodeAuthenticationRequest(const QString &username,
                                                  const QString &passwordHash,
                                                  quint32 authMethod);
    static bool decodeAuthenticationRequest(const QByteArray &bytes,
                                            AuthenticationRequest &out);

    // AuthenticationResponse 编码/解码
    static QByteArray encodeAuthenticationResponse(AuthResult result,
                                                   const QString &sessionId,
                                                   quint32 permissions);
    static bool decodeAuthenticationResponse(const QByteArray &bytes,
                                             AuthenticationResponse &out);

    // AuthChallenge 编码/解码
    static QByteArray encodeAuthChallenge(quint32 method,
                                          quint32 iterations,
                                          quint32 keyLength,
                                          const QByteArray &salt);
    static bool decodeAuthChallenge(const QByteArray &bytes, AuthChallenge &out);

    // HandshakeRequest/Response 字段级编码/解码
    static QByteArray encodeHandshakeRequest(const HandshakeRequest &req);
    static bool decodeHandshakeRequest(const QByteArray &bytes, HandshakeRequest &out);
    static QByteArray encodeHandshakeResponse(const HandshakeResponse &resp);
    static bool decodeHandshakeResponse(const QByteArray &bytes, HandshakeResponse &out);

    // MouseEvent 字段级编码/解码
    static QByteArray encodeMouseEvent(const MouseEvent &ev);
    static bool decodeMouseEvent(const QByteArray &bytes, MouseEvent &out);

    // KeyboardEvent 字段级编码/解码
    static QByteArray encodeKeyboardEvent(const KeyboardEvent &ev);
    static bool decodeKeyboardEvent(const QByteArray &bytes, KeyboardEvent &out);

    // ErrorMessage 字段级编码/解码
    static QByteArray encodeErrorMessage(quint32 errorCode, const QString &errorText);
    static bool decodeErrorMessage(const QByteArray &bytes, ErrorMessage &out);

    // StatusUpdate 字段级编码/解码（兼容旧版字符串负载）
    static QByteArray encodeStatusUpdate(const StatusUpdate &st);
    static bool decodeStatusUpdate(const QByteArray &bytes, StatusUpdate &out);

    // FileTransferRequest/Response 字段级编码/解码
    static QByteArray encodeFileTransferRequest(const FileTransferRequest &req);
    static bool decodeFileTransferRequest(const QByteArray &bytes, FileTransferRequest &out);
    static QByteArray encodeFileTransferResponse(const FileTransferResponse &resp);
    static bool decodeFileTransferResponse(const QByteArray &bytes, FileTransferResponse &out);

    // FileData（含数据块）字段级编码/解码
    static QByteArray encodeFileData(const FileData &hdr, const QByteArray &data);
    static bool decodeFileData(const QByteArray &bytes, FileData &hdrOut, QByteArray &dataOut);

    // ClipboardData（含数据块）字段级编码/解码
    static QByteArray encodeClipboardData(quint8 dataType, const QByteArray &data);
    static bool decodeClipboardData(const QByteArray &bytes, ClipboardData &metaOut, QByteArray &dataOut);
    
private:
    static quint32 s_sequenceNumber;
};

#endif // PROTOCOL_H