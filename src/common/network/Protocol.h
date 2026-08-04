#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtCore/QtGlobal>

// 取消Windows SDK中的宏定义,避免命名冲突
#ifdef _WIN32
#ifdef MOUSE_EVENT
#undef MOUSE_EVENT
#endif
#ifdef KEYBOARD_EVENT
#undef KEYBOARD_EVENT
#endif
#endif

// 消息类型枚举——按功能域分段编号，每域预留 0xNN01~0xNNFF 扩展空间。
// 低位域(0x00~0x30)为会话业务消息，高位域(0xF0)为传输层保活消息。
enum class MessageType : quint32 {
    // 连接与认证 (0x00xx)
    HANDSHAKE_REQUEST       = 0x0001,
    HANDSHAKE_RESPONSE      = 0x0002,   // 携带认证参数（salt/PBKDF2 参数）
    AUTHENTICATION_REQUEST  = 0x0003,
    AUTHENTICATION_RESPONSE = 0x0004,
    SESSION_CAPABILITIES    = 0x0005,   // 会话能力（编码参数）单向通知：客户端 → 服务端

    // 屏幕数据 (0x10xx)
    SCREEN_DATA             = 0x1001,
    CURSOR_SHAPE            = 0x1002,

    // 输入事件 (0x20xx)
    MOUSE_EVENT             = 0x2001,
    KEYBOARD_EVENT          = 0x2002,

    // 剪贴板 (0x30xx)
    CLIPBOARD_DATA          = 0x3001,

    // 心跳 (0xF0xx)——传输层保活，独立于会话业务消息
    HEARTBEAT               = 0xF001,
    HEARTBEAT_RESPONSE      = 0xF002
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
    WHEEL_DOWN = 0x09,
    LEFT_DOUBLE_CLICK = 0x0A,
    RIGHT_DOUBLE_CLICK = 0x0B,
    MIDDLE_DOUBLE_CLICK = 0x0C
};

// 键盘事件类型
enum class KeyboardEventType : quint8 {
    KEY_PRESS = 0x01,
    KEY_RELEASE = 0x02
};

// 认证结果
enum class AuthResult : quint8 {
    SUCCESS             = 0x00,
    INVALID_CREDENTIALS = 0x01,
    ACCESS_DENIED       = 0x02,
};

// 编解码接口：仅负责消息打包与从缓冲区解包
class IMessageCodec {
public:
    virtual ~IMessageCodec() = default;

    // 将类型与载荷编码为可发送的数据帧
    [[nodiscard]] virtual QByteArray encode() const = 0;

    // 从接收缓冲区尝试解析一帧，成功则填充header与payload，并从buffer移除已消费字节
    [[nodiscard]] virtual bool decode(const QByteArray& dataBuffer) = 0;
};

// 消息头结构
struct MessageHeader : public IMessageCodec {
    quint32 magic;          // 魔数
    MessageType type;       // 消息类型
    quint32 length;         // 数据长度
    quint32 checksum;       // 校验和
    quint64 timestamp;      // 时间戳

    [[nodiscard]] QByteArray encode() const;
    [[nodiscard]] bool decode(const QByteArray& dataBuffer);
};

//基础数据
struct BaseMessage : public IMessageCodec {
    QByteArray data;

    [[nodiscard]] QByteArray encode() const;
    [[nodiscard]] bool decode(const QByteArray& dataBuffer);
};

// 握手请求数据（身份/应用版本，不含协商参数）
struct HandshakeRequest : public IMessageCodec {
    QString appVersion;   // 应用版本（"x.y.z"，服务端校验与本机完全相等）
    QString clientName;
    QString clientOS;

    // 将当前结构体序列化为QByteArray（小端）
    [[nodiscard]] QByteArray encode() const;
    // 从QByteArray反序列化到当前结构体（小端）
    [[nodiscard]] bool decode(const QByteArray& dataBuffer);
};

// 握手响应数据（身份/应用版本 + 认证参数）
//
// PBKDF2 认证参数随握手响应下发：
// saltHex 为空 = 无密码模式（客户端等待服务端直通 AUTHENTICATION_RESPONSE）；
// saltHex 非空 = 密码模式（客户端以 salt/iterations/keyLength 派生后发认证请求）。
struct HandshakeResponse : public IMessageCodec {
    QString appVersion;   // 应用版本（"x.y.z"，客户端校验与本机完全相等）
    QString serverName;
    QString serverOS;

    // 认证参数（每连接动态生成）
    quint32 iterations = 0;   // PBKDF2 迭代次数，如 100000
    quint32 keyLength = 0;    // 派生密钥长度（字节），如 32
    QString saltHex;          // 盐值（hex 字符串；空 = 无密码模式）

    [[nodiscard]] QByteArray encode() const;
    [[nodiscard]] bool decode(const QByteArray& dataBuffer);
};

// 会话能力（编码参数）——认证成功后客户端单向通知服务端
struct SessionCapabilities : public IMessageCodec {
    quint8 imageQuality;  // JPEG 质量 1-100
    quint8 colorDepth;    // 色深 16/24/32

    // 将当前结构体序列化为QByteArray（小端）
    [[nodiscard]] QByteArray encode() const;
    // 从QByteArray反序列化到当前结构体（小端）
    [[nodiscard]] bool decode(const QByteArray& dataBuffer);
};

// 认证请求数据
struct AuthenticationRequest : public IMessageCodec {
    QString username;
    QString passwordHash;

    [[nodiscard]] QByteArray encode() const;
    [[nodiscard]] bool decode(const QByteArray& dataBuffer);
};

// 认证响应数据
struct AuthenticationResponse : public IMessageCodec {
    AuthResult result;
    QString sessionId;

    [[nodiscard]] QByteArray encode() const;
    [[nodiscard]] bool decode(const QByteArray& dataBuffer);
};

// 鼠标事件数据
struct MouseEvent : public IMessageCodec {
    MouseEventType eventType;
    qint16 x;
    qint16 y;
    qint16 wheelDelta;

    [[nodiscard]] QByteArray encode() const;
    [[nodiscard]] bool decode(const QByteArray& dataBuffer);
};

// 键盘事件数据
struct KeyboardEvent : public IMessageCodec {
    KeyboardEventType eventType;
    quint32 keyCode;
    quint32 modifiers;
    QString text;

    [[nodiscard]] QByteArray encode() const;
    [[nodiscard]] bool decode(const QByteArray& dataBuffer);
};

// 屏幕数据压缩类型标志
enum class ScreenDataFlags : quint8 {
    NONE = 0x00,           ///< 无特殊标志（仅JPEG压缩）
    SCALED = 0x02          ///< 图像已缩放（需要客户端放大显示）
};

// 屏幕数据
struct ScreenData : public IMessageCodec {
    quint16 x;
    quint16 y;
    quint16 width;             ///< 当前图像宽度（可能是缩放后的）
    quint16 height;            ///< 当前图像高度（可能是缩放后的）
    quint16 originalWidth;     ///< 原始图像宽度（缩放前）
    quint16 originalHeight;    ///< 原始图像高度（缩放前）
    quint32 dataSize;
    quint8 flags;              ///< 压缩标志 (ScreenDataFlags)
    quint64 captureTimestamp;  ///< 服务端捕获时间戳 (ms since epoch)，用于端到端延迟测量
    QByteArray imageData;

    ScreenData() : x(0), y(0), width(0), height(0), originalWidth(0), originalHeight(0), dataSize(0), flags(0), captureTimestamp(0) {}

    [[nodiscard]] QByteArray encode() const; // 附带数据体
    [[nodiscard]] bool decode(const QByteArray& dataBuffer);
};

struct CursorMessage : public IMessageCodec {
    qint32 posX     = 0;    // 服务端屏幕绝对坐标（客户端需映射到视口）
    qint32 posY     = 0;
    qint32 hotX     = 0;
    qint32 hotY     = 0;
    qint32 width    = 0;    // 0×0 = OS 隐藏光标 / 无变化
    qint32 height   = 0;
    QByteArray pixels;      // RGBA 原始像素，size = width*height*4

    CursorMessage() = default;
    CursorMessage(qint32 px, qint32 py, qint32 hx, qint32 hy, qint32 w, qint32 h, const QByteArray& p)
        : posX(px), posY(py), hotX(hx), hotY(hy), width(w), height(h), pixels(p) {}

    QByteArray encode() const override;
    bool decode(const QByteArray& data) override;
};

Q_DECLARE_METATYPE(CursorMessage)

// 剪贴板数据类型
enum class ClipboardDataType : quint8 {
    TEXT = 0x01,    // 文本数据
    IMAGE = 0x02    // 图片数据（PNG格式）
};

// 统一的剪贴板消息
struct ClipboardMessage : public IMessageCodec {
    ClipboardDataType dataType;
    QByteArray data;           // 统一的数据存储（文本使用UTF-8编码，图片使用PNG格式）
    quint32 width;             // 图片宽度（仅图片类型时有效）
    quint32 height;            // 图片高度（仅图片类型时有效）

    ClipboardMessage();
    explicit ClipboardMessage(const QString& text);
    ClipboardMessage(const QByteArray& imageData, quint32 w, quint32 h);

    bool isText() const;
    bool isImage() const;
    QString text() const;
    QByteArray imageData() const;

    QByteArray encode() const override;
    bool decode(const QByteArray& dataBuffer) override;
};

Q_DECLARE_METATYPE(ClipboardMessage)

// 协议工具类（TLS负责传输层加密，协议层不再加密）
class Protocol {
public:
    // 创建消息
    [[nodiscard]] static QByteArray createMessage(MessageType type, const IMessageCodec& message);

    // 解析消息
    [[nodiscard]] static qsizetype parseMessage(const QByteArray& data, MessageHeader& header, QByteArray& payload);

private:
    // 验证接收数据的完整性（检查数据长度是否完整）
    // 参数：data - 接收缓冲区数据，header - 输出参数，数据完整时填充消息头信息
    // 返回值：-1 表示数据不完整需要等待更多数据，0 表示数据无效，>0 表示完整消息的总长度
    static qsizetype validateReceivedDataIntegrity(const QByteArray& data, MessageHeader& header);

    // 计算校验和
    static quint32 calculateChecksum(const QByteArray& data);
};

