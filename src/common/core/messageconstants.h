#ifndef MESSAGECONSTANTS_H
#define MESSAGECONSTANTS_H

#include <QtCore/QString>

namespace MessageConstants {
    // 通用错误消息
    namespace Common {
        const QString ALREADY_CONNECTED = "Already connected or connecting";
        const QString NOT_CONNECTED = "Not connected to server";
        const QString CONNECTION_FAILED = "Connection failed";
        const QString AUTHENTICATION_FAILED = "Authentication failed";
        const QString INVALID_DATA = "Invalid data";
        const QString OPERATION_FAILED = "Operation failed";
        const QString UNSUPPORTED_OPERATION = "Unsupported operation";
    }

    // 压缩相关消息
    namespace Compression {
        const QString UNSUPPORTED_ALGORITHM = "Unsupported compression algorithm";
        const QString INVALID_COMPRESSION_LEVEL = "Invalid compression level. Valid range: 0-9";
        const QString INVALID_WINDOW_BITS = "Invalid window bits. Valid range: 8-15";
        const QString INVALID_MEMORY_LEVEL = "Invalid memory level. Valid range: 1-9";
        const QString ZLIB_INIT_FAILED = "Failed to initialize zlib compression";
        const QString ZLIB_COMPRESSION_FAILED = "Zlib compression failed with error:";
        const QString ZLIB_DECOMPRESSION_FAILED = "Zlib decompression failed with error:";
        const QString ZLIB_DECOMPRESSION_INCOMPLETE = "Zlib decompression incomplete";
    // Removed legacy LZ4/ZSTD-specific constants (unused after refactor)
        const QString ALGORITHM_DETECTION_FAILED = "Cannot detect compression algorithm";
    }

    // 加密相关消息
    namespace Encryption {
        inline QString invalidKeySize(int expected, int got) {
            return QString("Invalid key size. Expected: %1 Got: %2").arg(expected).arg(got);
        }
        const QString UNSUPPORTED_KEY_SIZE = "Invalid key size. Supported sizes: 128, 192, 256";
        const QString FAILED_GENERATE_KEY = "Failed to generate random key";
        const QString FAILED_GENERATE_IV = "Failed to generate random IV";
        const QString NO_KEY_SET_ENCRYPTION = "No key set for encryption";
        const QString NO_KEY_SET_DECRYPTION = "No key set for decryption";
        const QString FAILED_CREATE_CONTEXT = "Failed to create cipher context";
        const QString FAILED_INIT_ENCRYPTION = "Failed to initialize encryption";
        const QString FAILED_INIT_DECRYPTION = "Failed to initialize decryption";
        const QString FAILED_ENCRYPT_DATA = "Failed to encrypt data";
        const QString FAILED_DECRYPT_DATA = "Failed to decrypt data";
        const QString FAILED_FINALIZE_ENCRYPTION = "Failed to finalize encryption";
        const QString FAILED_FINALIZE_DECRYPTION = "Failed to finalize decryption";
        const QString DATA_TOO_SMALL = "Encrypted data too small to contain IV";
        const QString UNSUPPORTED_KEY_SIZE_OR_MODE = "Unsupported key size or mode";
        const QString FAILED_GENERATE_KEYPAIR = "Failed to generate key pair";
        const QString FAILED_PARSE_PUBLIC_KEY = "Failed to parse public key";
        const QString FAILED_PARSE_PRIVATE_KEY = "Failed to parse private key";
        const QString NO_PUBLIC_KEY_SET = "No public key set for encryption";
        const QString NO_PRIVATE_KEY_SET = "No private key set for decryption";
        const QString FAILED_SET_PADDING = "Failed to set padding mode";
        const QString FAILED_CREATE_KEY_CONTEXT = "Failed to create key generation context";
        const QString FAILED_INIT_KEY_GENERATION = "Failed to initialize key generation";
        const QString FAILED_SET_KEY_SIZE = "Failed to set key size";
        const QString FAILED_GENERATE_KEY_PAIR = "Failed to generate key pair";
        const QString FAILED_CREATE_BIO_PUBLIC = "Failed to create BIO for public key";
        const QString FAILED_CREATE_BIO_PRIVATE = "Failed to create BIO for private key";
        const QString NO_PUBLIC_KEY_ENCRYPTION = "No public key set for encryption";
        const QString FAILED_CREATE_ENCRYPT_CONTEXT = "Failed to create encryption context";
        const QString FAILED_INIT_RSA_ENCRYPTION = "Failed to initialize encryption";
        const QString FAILED_DETERMINE_OUTPUT_LENGTH = "Failed to determine output length";
        const QString FAILED_RSA_ENCRYPT_DATA = "Failed to encrypt data";
        const QString NO_PRIVATE_KEY_DECRYPTION = "No private key set for decryption";
        const QString FAILED_CREATE_DECRYPT_CONTEXT = "Failed to create decryption context";
        const QString FAILED_INIT_RSA_DECRYPTION = "Failed to initialize decryption";
        const QString FAILED_RSA_DECRYPT_DATA = "Failed to decrypt data";
        const QString NO_PRIVATE_KEY_SIGNING = "No private key set for signing";
        const QString FAILED_CREATE_SIGN_CONTEXT = "Failed to create signing context";
        const QString FAILED_INIT_SIGNING = "Failed to initialize signing";
        const QString FAILED_UPDATE_SIGNING = "Failed to update signing";
        const QString FAILED_DETERMINE_SIGNATURE_LENGTH = "Failed to determine signature length";
        const QString FAILED_CREATE_SIGNATURE = "Failed to create signature";
        const QString NO_PUBLIC_KEY_VERIFICATION = "No public key set for verification";
        const QString FAILED_CREATE_VERIFY_CONTEXT = "Failed to create verification context";
        const QString FAILED_INIT_VERIFICATION = "Failed to initialize verification";
        const QString FAILED_UPDATE_VERIFICATION = "Failed to update verification";
        const QString PBKDF2_DERIVATION_FAILED = "PBKDF2 key derivation failed";
        const QString FAILED_GENERATE_RANDOM_BYTES = "Failed to generate random bytes";
    }

    // 网络相关消息
    namespace Network {
        const QString SERVER_ALREADY_RUNNING = "Server already running";
        const QString FAILED_START_SERVER = "Failed to start server:";
        const QString SERVER_STARTED_SUCCESS = "Server successfully started on port:";
        const QString STOPPING_SERVER = "Stopping server, synchronous:";
        const QString SERVER_STOPPED = "Server stopped successfully";
        const QString REJECTING_CONNECTION_MULTIPLE = "Rejecting connection - multiple clients not allowed";
        const QString REJECTING_CONNECTION_MAX = "Rejecting connection - max clients reached:";
        const QString CLIENT_HANDLER_CREATED = "Created ClientHandler for client:";
        const QString FAILED_SET_SOCKET = "Failed to set socket descriptor, error:";
        const QString SOCKET_SET_SUCCESS = "Socket descriptor set successfully";
        const QString UNHANDLED_MESSAGE_TYPE = "Unhandled message type:";
        const QString CHECKSUM_MISMATCH = "Checksum mismatch. Expected: %1 Got: %2";
        const QString CONNECTION_ESTABLISHED = "连接已建立";
        const QString CONNECTION_LOST = "连接丢失";
        const QString INVALID_MESSAGE_FORMAT = "无效的消息格式";
        const QString HANDSHAKE_FAILED = "握手失败";
        const QString TIMEOUT_ERROR = "连接超时";
        const QString NETWORK_ERROR = "网络错误";
        const QString CONNECTING_TO = "TcpClient: 正在连接到 %1:%2";
        const QString CONNECTION_SUCCESSFUL = "TcpClient: 连接成功";
        const QString CONNECTION_FAILED = "TcpClient: 连接失败 - %1";
        const QString CONNECTION_DISCONNECTED = "TcpClient: 连接断开";
        const QString ALREADY_CONNECTED = "Already connected or connecting";
        const QString NOT_CONNECTED = "Not connected to server";
        const QString HANDSHAKE_RESPONSE_RECEIVED = "Received handshake response from server";
        const QString AUTH_RESPONSE_RECEIVED = "Received authentication response from server";
        const QString AUTH_SUCCESSFUL = "Authentication successful, session ID: %1";
        const QString HEARTBEAT_RECEIVED = "Received heartbeat from server";
        const QString DISCONNECT_REQUEST_RECEIVED = "Received disconnect request from server";
        const QString HANDSHAKE_REQUEST_SENT = "Sent handshake request to server";
        const QString AUTH_REQUEST_SENT = "Sent authentication request to server for user: %1";
    }

    // 会话管理消息
    namespace Session {
        const QString SESSION_ALREADY_ACTIVE = "Session already active or starting";
        const QString CANNOT_START_NOT_AUTH = "Cannot start session - not authenticated";
        const QString SESSION_NOT_ACTIVE = "Session not active, ignoring";
        const QString EMPTY_SCREEN_DATA = "Received empty screen data";
        const QString PROCESSING_SCREEN_DATA = "Processing screen data, size:";
        const QString DECOMPRESSION_FAILED = "Failed to decompress screen data, trying fallback methods";
        const QString LOADED_UNCOMPRESSED = "Successfully loaded uncompressed screen data";
        const QString DECOMPRESSION_SUCCESS = "Successfully decompressed with algorithm:";
        const QString ALL_DECOMPRESSION_FAILED = "All decompression attempts failed";
        const QString SCREEN_DATA_LOADED = "Successfully loaded screen data, decompressed size:";
        const QString FAILED_LOAD_SCREEN_DATA = "Failed to load screen data from decompressed bytes, size:";
    }

    // 屏幕捕获消息
    namespace ScreenCapture {
        const QString ALREADY_CAPTURING = "Already capturing, ignoring start request";
        const QString STARTING_CAPTURE = "Starting capture with interval:";
        const QString CAPTURE_STARTED = "Capture started successfully";
        const QString ALREADY_STOPPED = "Already stopped, ignoring stop request";
        const QString STOPPING_CAPTURE = "Stopping capture";
        const QString CAPTURE_STOPPED = "Capture stopped successfully";
        const QString NO_PRIMARY_SCREEN = "No primary screen found";
        const QString FRAME_CAPTURED = "Frame captured (count: %1), size: %2, quality: %3";
        const QString CAPTURE_FAILED = "Failed to capture frame - screenshot is null (failures: %1)";
        const QString FRAME_RATE_SET = "Frame rate set to %1 FPS, interval: %2 ms";
        const QString QUALITY_SET = "Capture quality set to %1";
    }

    // 配置相关消息
    namespace Config {
        const QString FILE_NOT_EXIST = "Config file does not exist:";
        const QString FAILED_OPEN_READ = "Failed to open config file for reading:";
        const QString FAILED_DECRYPT = "Failed to decrypt config file";
        const QString BINARY_NOT_SUPPORTED = "Binary format not supported";
        const QString JSON_NOT_IMPLEMENTED = "JSON format save not implemented";
        const QString XML_NOT_IMPLEMENTED = "XML format save not implemented";
        const QString JSON_LOAD_NOT_IMPLEMENTED = "JSON format load not implemented";
        const QString XML_LOAD_NOT_IMPLEMENTED = "XML format load not implemented";
    }

    // 日志相关消息
    namespace Logger {
        const QString FAILED_OPEN_LOG_FILE = "Failed to open log file:";
    }

    // UI相关消息
    namespace UI {
        const QString WINDOW_TITLE = "远程桌面 - %1";
        const QString CONNECTION_ERROR_TITLE = "连接错误";
        const QString CONNECTION_ERROR_MESSAGE = "远程桌面连接发生错误:\n%1";
        const QString CONNECTION_ERROR_PREFIX = "连接错误:";
        const QString CLOSE_WINDOW_TITLE = "关闭远程桌面";
        const QString CLOSE_WINDOW_MESSAGE = "关闭窗口将断开远程桌面连接。确定要继续吗？";
        const QString CONNECTION_CLOSED = "连接已断开";
        const QString PIXMAP_ITEM_NULL = "ClientRemoteWindow::setRemoteScreen - ERROR: m_pixmapItem is null!";
        const QString FIT_TO_WINDOW = "适应窗口";
        const QString ACTUAL_SIZE = "实际大小";
        const QString ZOOM_IN = "放大";
        const QString ZOOM_OUT = "缩小";
        const QString FULL_SCREEN = "全屏";
        const QString SCREENSHOT = "截图";
        const QString DISCONNECT = "断开连接";
        const QString RECONNECT = "重新连接";
        const QString SETTINGS = "设置";
        const QString STATUS_CONNECTING = "正在连接...";
        const QString STATUS_CONNECTED = "已连接";
        const QString STATUS_AUTHENTICATING = "正在认证...";
        const QString STATUS_AUTHENTICATED = "已认证";
        const QString STATUS_RECONNECTING = "正在重连...";
        const QString STATUS_DISCONNECTING = "正在断开连接...";
        const QString STATUS_DISCONNECTED = "未连接";
        const QString STATUS_ERROR = "连接错误";
        const QString CONNECTION_ID_INFO = "连接ID: %1";
        const QString MAIN_WINDOW_CONSTRUCTED = "MainWindow constructed";
        const QString MAIN_WINDOW_DESTROYED = "MainWindow destroyed";
        const QString START_ERROR_TITLE = "启动错误";
        const QString START_SERVER_ERROR = "无法启动服务器：%1";
        const QString SERVER_STATUS_TITLE = "服务器状态";
        const QString SERVER_STARTED = "服务器已在端口 %1 上启动";
        const QString SERVER_STOPPED = "服务器已停止";
        const QString SERVER_ALREADY_RUNNING = "服务器已经在运行中。";
        const QString ERROR_TITLE = "错误";
        const QString SERVER_MANAGER_NOT_INITIALIZED = "服务器管理器未初始化。";
        const QString SERVER_NOT_RUNNING = "服务器未运行。";
        const QString INPUT_ERROR_TITLE = "输入错误";
        const QString INVALID_HOST_ADDRESS = "请输入有效的主机地址";
        const QString INVALID_PORT_RANGE = "端口号必须在1-65535之间";
        const QString VALIDATION_ERROR_TITLE = "验证错误";
    }
}

#endif // MESSAGECONSTANTS_H