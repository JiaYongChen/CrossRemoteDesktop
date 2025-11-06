#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QtCore/QByteArray>
#include <QtCore/QDataStream>
#include <QtCore/QIODevice>
#include <QtCore/qglobal.h>

// 取消Windows SDK中的宏定义,避免命名冲突
#ifdef _WIN32
#ifdef MOUSE_EVENT
#undef MOUSE_EVENT
#endif
#ifdef KEYBOARD_EVENT
#undef KEYBOARD_EVENT
#endif
#endif

// 协议版本
#define PROTOCOL_VERSION 1

// 魔数用于验证数据包
#define PROTOCOL_MAGIC 0x52444350  // "RDCP" in hex

// 序列化后的消息头大小：5个quint32字段 + 1个quint64字段 = 28字节
#define SERIALIZED_HEADER_SIZE (5 * sizeof(quint32) + sizeof(quint64))

// 消息类型枚举
enum class MessageType : quint32 {
    // 连接管理
    HANDSHAKE_REQUEST = 0x0001,
    HANDSHAKE_RESPONSE = 0x0002,
    AUTHENTICATION_REQUEST = 0x0003,
    AUTHENTICATION_RESPONSE = 0x0004,
    HEARTBEAT = 0x0006,
    HEARTBEAT_RESPONSE = 0x0007,
    AUTH_CHALLENGE = 0x0008,

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
    CLIPBOARD_DATA = 0x5001
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

// 编解码接口：仅负责消息打包与从缓冲区解包
class IMessageCodec {
public:
    virtual ~IMessageCodec() = default;

    // 将类型与载荷编码为可发送的数据帧
    virtual QByteArray encode() const = 0;

    // 从接收缓冲区尝试解析一帧，成功则填充header与payload，并从buffer移除已消费字节
    virtual bool decode(const QByteArray& dataBuffer) = 0;
};

// 消息头结构
struct MessageHeader : public IMessageCodec {
    quint32 magic;          // 魔数
    quint32 version;        // 协议版本
    MessageType type;       // 消息类型
    quint32 length;         // 数据长度
    quint32 checksum;       // 校验和
    quint64 timestamp;      // 时间戳

    QByteArray encode() const;
    bool decode(const QByteArray& dataBuffer);
};

//基础数据
struct BaseMessage : public IMessageCodec {
    QByteArray data;

    QByteArray encode() const;
    bool decode(const QByteArray& dataBuffer);
};

// 握手请求数据
struct HandshakeRequest : public IMessageCodec {
    quint32 clientVersion;
    quint16 screenWidth;
    quint16 screenHeight;
    quint8 colorDepth;
    char clientName[64];
    char clientOS[32];

    // 将当前结构体序列化为QByteArray（小端）
    QByteArray encode() const;
    // 从QByteArray反序列化到当前结构体（小端）
    bool decode(const QByteArray& dataBuffer);
};

// 握手响应数据
struct HandshakeResponse : public IMessageCodec {
    quint32 serverVersion;
    quint16 screenWidth;
    quint16 screenHeight;
    quint8 colorDepth;
    quint8 supportedFeatures;
    char serverName[64];
    char serverOS[32];

    QByteArray encode() const;
    bool decode(const QByteArray& dataBuffer);
};

// 认证请求数据
struct AuthenticationRequest : public IMessageCodec {
    char username[64];
    char passwordHash[64];
    quint32 authMethod;

    QByteArray encode() const;
    bool decode(const QByteArray& dataBuffer);
};

// 认证响应数据
struct AuthenticationResponse : public IMessageCodec {
    AuthResult result;
    char sessionId[32];
    quint32 permissions;

    QByteArray encode() const;
    bool decode(const QByteArray& dataBuffer);
};

// 认证挑战（阶段C：PBKDF2 握手参数）
struct AuthChallenge : public IMessageCodec {
    quint32 method;      // 1=PBKDF2_SHA256（约定）
    quint32 iterations;  // 推荐 100000
    quint32 keyLength;   // 派生长度（字节），如 32
    char    saltHex[64]; // 盐（hex字符串，最多32字节盐=64字符）

    QByteArray encode() const;
    bool decode(const QByteArray& dataBuffer);
};

// 鼠标事件数据
struct MouseEvent : public IMessageCodec {
    MouseEventType eventType;
    qint16 x;
    qint16 y;
    qint16 wheelDelta;

    QByteArray encode() const;
    bool decode(const QByteArray& dataBuffer);
};

// 键盘事件数据
struct KeyboardEvent : public IMessageCodec {
    KeyboardEventType eventType;
    quint32 keyCode;
    quint32 modifiers;
    char text[8];

    QByteArray encode() const;
    bool decode(const QByteArray& dataBuffer);
};

// 屏幕数据
struct ScreenData : public IMessageCodec {
    quint16 x;
    quint16 y;
    quint16 width;
    quint16 height;
    quint32 dataSize;
    QByteArray imageData;

    QByteArray encode() const; // 附带数据体
    bool decode(const QByteArray& dataBuffer);
};

// 音频数据
struct AudioData : public IMessageCodec {
    quint32 sampleRate;
    quint8 channels;
    quint8 bitsPerSample;
    quint32 dataSize;
    // 实际音频数据跟在后面

    QByteArray encode() const;
    bool decode(const QByteArray& dataBuffer);
};

// 文件传输请求
struct FileTransferRequest : public IMessageCodec {
    char fileName[256];
    quint64 fileSize;
    quint32 transferId;
    quint8 direction;

    QByteArray encode() const;
    bool decode(const QByteArray& dataBuffer);
};

// 文件传输响应
struct FileTransferResponse : public IMessageCodec {
    quint32 transferId;
    FileTransferStatus status;
    char errorMessage[256];

    QByteArray encode() const;
    bool decode(const QByteArray& dataBuffer);
};

// 文件数据块
struct FileData : public IMessageCodec {
    quint32 transferId;
    quint64 offset;
    quint32 dataSize;
    // 实际文件数据跟在后面

    QByteArray encode() const;
    bool decode(const QByteArray& dataBuffer);
};

// 剪贴板数据
struct ClipboardData : public IMessageCodec {
    quint8 dataType;
    quint32 dataSize;
    // 实际剪贴板数据跟在后面

    QByteArray encode() const;
    bool decode(const QByteArray& dataBuffer);
};

// 协议工具类
class Protocol {
public:
    // 创建消息
    static QByteArray createMessage(MessageType type, const IMessageCodec& message);

    // 解析消息
    static qsizetype parseMessage(const QByteArray& data, MessageHeader& header, QByteArray& payload);

    // 加密数据
    static QByteArray encryptData(const QByteArray& data, const QByteArray& key);

    // 解密数据
    static QByteArray decryptData(const QByteArray& data, const QByteArray& key);

private:
    // 验证接收数据的完整性（检查数据长度是否完整）
    // 参数：data - 接收缓冲区数据，header - 输出参数，数据完整时填充消息头信息
    // 返回值：-1 表示数据不完整需要等待更多数据，0 表示数据无效，>0 表示完整消息的总长度
    static qsizetype validateReceivedDataIntegrity(const QByteArray& data, MessageHeader& header);

    // 计算校验和
    static quint32 calculateChecksum(const QByteArray& data);

private:
    static const QByteArray XORkey;
};

#endif // PROTOCOL_H