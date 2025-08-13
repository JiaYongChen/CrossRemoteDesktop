#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <QByteArray>
#include <QString>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QObject>

// Forward declarations for OpenSSL types
typedef struct evp_cipher_st EVP_CIPHER;
typedef struct evp_pkey_st EVP_PKEY;
typedef struct rsa_st RSA;

class Encryption
{
public:
    // AES加密算法
    enum AESKeySize {
        AES128 = 128,
        AES192 = 192,
        AES256 = 256
    };
    
    enum AESMode {
        ECB,
        CBC,
        CFB,
        OFB,
        CTR,
        GCM
    };
    
    // 哈希算法
    enum HashAlgorithm {
        MD5,
        SHA1,
        SHA224,
        SHA256,
        SHA384,
        SHA512,
        SHA3_224,
        SHA3_256,
        SHA3_384,
        SHA3_512
    };
    
    // AES加密/解密
    static QByteArray encryptAES(const QByteArray &data, const QByteArray &key, 
                                const QByteArray &iv = QByteArray(), 
                                AESKeySize keySize = AES256, 
                                AESMode mode = CBC);
    
    static QByteArray decryptAES(const QByteArray &encryptedData, const QByteArray &key, 
                                const QByteArray &iv = QByteArray(), 
                                AESKeySize keySize = AES256, 
                                AESMode mode = CBC);
    
    // RSA加密/解密（用于密钥交换）
    static QByteArray encryptRSA(const QByteArray &data, const QByteArray &publicKey);
    static QByteArray decryptRSA(const QByteArray &encryptedData, const QByteArray &privateKey);
    
    // RSA密钥对生成
    static QPair<QByteArray, QByteArray> generateRSAKeyPair(int keySize = 2048);
    
    // 哈希函数
    static QByteArray hash(const QByteArray &data, HashAlgorithm algorithm = SHA256);
    static QString hashString(const QString &data, HashAlgorithm algorithm = SHA256);
    
    // HMAC
    static QByteArray hmac(const QByteArray &data, const QByteArray &key, HashAlgorithm algorithm = SHA256);
    
    // 密钥派生函数 (PBKDF2)
    static QByteArray deriveKey(const QString &password, const QByteArray &salt, 
                               int iterations = 10000, int keyLength = 32, 
                               HashAlgorithm algorithm = SHA256);
    
    // 随机数生成
    static QByteArray generateRandomBytes(int length);
    static QByteArray generateSalt(int length = 16);
    static QByteArray generateIV(int length = 16);
    static QString generateRandomString(int length, bool alphaNumericOnly = true);
    
    // 密码强度检查
    enum PasswordStrength {
        VeryWeak,
        Weak,
        Medium,
        Strong,
        VeryStrong
    };
    
    static PasswordStrength checkPasswordStrength(const QString &password);
    static QString passwordStrengthString(PasswordStrength strength);
    
    // Base64编码/解码
    static QString encodeBase64(const QByteArray &data);
    static QByteArray decodeBase64(const QString &encodedData);
    
    // Hex编码/解码
    static QString encodeHex(const QByteArray &data);
    static QByteArray decodeHex(const QString &hexData);
    
    // 数字签名
    static QByteArray signData(const QByteArray &data, const QByteArray &privateKey);
    static bool verifySignature(const QByteArray &data, const QByteArray &signature, const QByteArray &publicKey);
    
    // 密钥交换 (Diffie-Hellman)
    struct DHKeyPair {
        QByteArray publicKey;
        QByteArray privateKey;
    };
    
    static DHKeyPair generateDHKeyPair();
    static QByteArray computeDHSharedSecret(const QByteArray &privateKey, const QByteArray &otherPublicKey);
    
    // 安全随机密码生成
    static QString generateSecurePassword(int length = 12, bool includeSymbols = true);
    
    // 时间安全的字符串比较
    static bool secureCompare(const QByteArray &a, const QByteArray &b);
    static bool secureCompare(const QString &a, const QString &b);
    
    // 内存清理
    static void secureMemoryClear(QByteArray &data);
    static void secureMemoryClear(QString &data);
    
    // 错误处理
    static QString lastError();
    
private:
    // 内部辅助函数
    static QCryptographicHash::Algorithm hashAlgorithmToQt(HashAlgorithm algorithm);
    static bool isValidKeySize(const QByteArray &key, AESKeySize keySize);
    static QByteArray padPKCS7(const QByteArray &data, int blockSize);
    static QByteArray unpadPKCS7(const QByteArray &data);
    
    // OpenSSL相关（如果可用）
#ifdef OPENSSL_AVAILABLE
    static bool initializeOpenSSL();
    static void cleanupOpenSSL();
    static QByteArray encryptAESOpenSSL(const QByteArray &data, const QByteArray &key, 
                                       const QByteArray &iv, AESKeySize keySize, AESMode mode);
    static QByteArray decryptAESOpenSSL(const QByteArray &encryptedData, const QByteArray &key, 
                                       const QByteArray &iv, AESKeySize keySize, AESMode mode);
#endif
    
    // 错误信息
    static thread_local QString s_lastError;
    
    // 初始化标志
    static bool s_initialized;
    
    // 禁止实例化
    Encryption() = delete;
    ~Encryption() = delete;
    Encryption(const Encryption&) = delete;
    Encryption& operator=(const Encryption&) = delete;
};

// AES加密类
class AESEncryption : public QObject
{
    Q_OBJECT
    
public:
    enum Mode {
        ECB,
        CBC,
        CFB,
        OFB,
        CTR,
        GCM
    };
    
    explicit AESEncryption(QObject *parent = nullptr);
    ~AESEncryption();
    
    bool setKey(const QByteArray &key);
    QByteArray key() const;
    
    void setKeySize(int size);
    int keySize() const;
    
    void setMode(Mode mode);
    Mode mode() const;
    
    QByteArray generateKey();
    QByteArray generateIV();
    
    QByteArray encrypt(const QByteArray &data, const QByteArray &iv = QByteArray());
    QByteArray decrypt(const QByteArray &encryptedData, const QByteArray &iv = QByteArray());
    
private:
    const EVP_CIPHER* getCipher() const;
    
    QByteArray m_key;
    int m_keySize;
    Mode m_mode;
};

// RSA加密类
class RSAEncryption : public QObject
{
    Q_OBJECT
    
public:
    enum Padding {
        PKCS1,
        OAEP,
        PSS,
        None
    };
    
    explicit RSAEncryption(QObject *parent = nullptr);
    ~RSAEncryption();
    
    bool generateKeyPair();
    bool setPublicKey(const QByteArray &keyData);
    bool setPrivateKey(const QByteArray &keyData, const QString &password = QString());
    
    QByteArray getPublicKey() const;
    QByteArray getPrivateKey(const QString &password = QString()) const;
    
    QByteArray encrypt(const QByteArray &data) const;
    QByteArray decrypt(const QByteArray &encryptedData) const;
    
    QByteArray sign(const QByteArray &data) const;
    bool verify(const QByteArray &data, const QByteArray &signature) const;
    
    void setKeySize(int size) { m_keySize = size; }
    int keySize() const { return m_keySize; }
    
    void setPadding(Padding padding) { m_padding = padding; }
    Padding padding() const { return m_padding; }
    
private:
    int getPaddingMode() const;
    
    int m_keySize;
    Padding m_padding;
    EVP_PKEY *m_publicKey;
    EVP_PKEY *m_privateKey;
};

// 哈希生成器类
class HashGenerator
{
public:
    static QByteArray md5(const QByteArray &data);
    static QByteArray sha1(const QByteArray &data);
    static QByteArray sha256(const QByteArray &data);
    static QByteArray sha512(const QByteArray &data);
    
    static QByteArray hmacSha256(const QByteArray &data, const QByteArray &key);
    static QByteArray pbkdf2(const QByteArray &password, const QByteArray &salt, int iterations, int keyLength);
    
private:
    HashGenerator() = delete;
};

// 随机数生成器类
class RandomGenerator
{
public:
    static QByteArray generateBytes(int size);
    static int generateInt(int min, int max);
    static QString generateString(int length, const QString &charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    static QByteArray generateSalt(int size = 16);
    
private:
    RandomGenerator() = delete;
};

// 安全字符串类（自动清理内存）
class SecureString
{
public:
    SecureString();
    explicit SecureString(const QString &str);
    explicit SecureString(const char *str);
    SecureString(const SecureString &other);
    SecureString(SecureString &&other) noexcept;
    ~SecureString();
    
    SecureString& operator=(const SecureString &other);
    SecureString& operator=(SecureString &&other) noexcept;
    SecureString& operator=(const QString &str);
    SecureString& operator=(const char *str);
    
    bool operator==(const SecureString &other) const;
    bool operator!=(const SecureString &other) const;
    
    QString toString() const;
    QByteArray toUtf8() const;
    const char* toCString() const;
    
    int length() const;
    bool isEmpty() const;
    void clear();
    
    void append(const QString &str);
    void append(const SecureString &other);
    
private:
    void secureDelete();
    
    char *m_data;
    int m_length;
    int m_capacity;
};

#endif // ENCRYPTION_H