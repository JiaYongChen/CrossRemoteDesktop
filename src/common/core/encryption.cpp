#include "encryption.h"
#include "messageconstants.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDebug>
#include "logging_categories.h"
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <QMessageLogger>

// AESEncryption 实现
AESEncryption::AESEncryption(QObject *parent)
    : QObject(parent)
    , m_keySize(256)
    , m_mode(Mode::CBC)
{
}

AESEncryption::~AESEncryption()
{
}

bool AESEncryption::setKey(const QByteArray &key)
{
    if (key.size() != m_keySize / 8) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::invalidKeySize(m_keySize / 8, key.size());
        return false;
    }
    
    m_key = key;
    return true;
}

QByteArray AESEncryption::key() const
{
    return m_key;
}

void AESEncryption::setKeySize(int size)
{
    if (size == 128 || size == 192 || size == 256) {
        m_keySize = size;
    } else {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::UNSUPPORTED_KEY_SIZE;
    }
}

int AESEncryption::keySize() const
{
    return m_keySize;
}

void AESEncryption::setMode(Mode mode)
{
    m_mode = mode;
}

AESEncryption::Mode AESEncryption::mode() const
{
    return m_mode;
}

QByteArray AESEncryption::generateKey()
{
    QByteArray key(m_keySize / 8, 0);
    
    if (RAND_bytes(reinterpret_cast<unsigned char*>(key.data()), key.size()) != 1) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_GENERATE_KEY;
        return QByteArray();
    }
    
    return key;
}

QByteArray AESEncryption::generateIV()
{
    QByteArray iv(AES_BLOCK_SIZE, 0);
    
    if (RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), iv.size()) != 1) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_GENERATE_IV;
        return QByteArray();
    }
    
    return iv;
}

QByteArray AESEncryption::encrypt(const QByteArray &data, const QByteArray &iv)
{
    if (m_key.isEmpty()) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::NO_KEY_SET_ENCRYPTION;
        return QByteArray();
    }
    
    QByteArray actualIV = iv;
    if (actualIV.isEmpty()) {
        actualIV = generateIV();
    }
    
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_CREATE_CONTEXT;
        return QByteArray();
    }
    
    const EVP_CIPHER *cipher = getCipher();
    if (!cipher) {
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    if (EVP_EncryptInit_ex(ctx, cipher, nullptr,
                          reinterpret_cast<const unsigned char*>(m_key.data()),
                          reinterpret_cast<const unsigned char*>(actualIV.data())) != 1) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_INIT_ENCRYPTION;
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    QByteArray encrypted;
    encrypted.resize(data.size() + AES_BLOCK_SIZE);
    
    int len;
    int encryptedLen = 0;
    
    if (EVP_EncryptUpdate(ctx,
                         reinterpret_cast<unsigned char*>(encrypted.data()),
                         &len,
                         reinterpret_cast<const unsigned char*>(data.data()),
                         data.size()) != 1) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_ENCRYPT_DATA;
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    encryptedLen = len;
    
    if (EVP_EncryptFinal_ex(ctx,
                           reinterpret_cast<unsigned char*>(encrypted.data()) + len,
                           &len) != 1) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_FINALIZE_ENCRYPTION;
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    encryptedLen += len;
    
    EVP_CIPHER_CTX_free(ctx);
    
    encrypted.resize(encryptedLen);
    
    // 如果生成了新的IV，将其添加到加密数据前面
    if (iv.isEmpty()) {
        return actualIV + encrypted;
    }
    
    return encrypted;
}

QByteArray AESEncryption::decrypt(const QByteArray &encryptedData, const QByteArray &iv)
{
    if (m_key.isEmpty()) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::NO_KEY_SET_DECRYPTION;
        return QByteArray();
    }
    
    QByteArray actualIV = iv;
    QByteArray dataToDecrypt = encryptedData;
    
    // 如果没有提供IV，从加密数据中提取
    if (actualIV.isEmpty()) {
        if (encryptedData.size() < AES_BLOCK_SIZE) {
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::DATA_TOO_SMALL;
            return QByteArray();
        }
        actualIV = encryptedData.left(AES_BLOCK_SIZE);
        dataToDecrypt = encryptedData.mid(AES_BLOCK_SIZE);
    }
    
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_CREATE_CONTEXT;
        return QByteArray();
    }
    
    const EVP_CIPHER *cipher = getCipher();
    if (!cipher) {
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    if (EVP_DecryptInit_ex(ctx, cipher, nullptr,
                          reinterpret_cast<const unsigned char*>(m_key.data()),
                          reinterpret_cast<const unsigned char*>(actualIV.data())) != 1) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_INIT_DECRYPTION;
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    QByteArray decrypted;
    decrypted.resize(dataToDecrypt.size() + AES_BLOCK_SIZE);
    
    int len;
    int decryptedLen = 0;
    
    if (EVP_DecryptUpdate(ctx,
                         reinterpret_cast<unsigned char*>(decrypted.data()),
                         &len,
                         reinterpret_cast<const unsigned char*>(dataToDecrypt.data()),
                         dataToDecrypt.size()) != 1) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_DECRYPT_DATA;
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    decryptedLen = len;
    
    if (EVP_DecryptFinal_ex(ctx,
                           reinterpret_cast<unsigned char*>(decrypted.data()) + len,
                           &len) != 1) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_FINALIZE_DECRYPTION;
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    decryptedLen += len;
    
    EVP_CIPHER_CTX_free(ctx);
    
    decrypted.resize(decryptedLen);
    return decrypted;
}

const EVP_CIPHER* AESEncryption::getCipher() const
{
    switch (m_keySize) {
    case 128:
        switch (m_mode) {
        case Mode::ECB: return EVP_aes_128_ecb();
        case Mode::CBC: return EVP_aes_128_cbc();
        case Mode::CFB: return EVP_aes_128_cfb();
        case Mode::OFB: return EVP_aes_128_ofb();
        case Mode::GCM: return EVP_aes_128_gcm();
        case Mode::CTR: return EVP_aes_128_ctr();
        }
        break;
    case 192:
        switch (m_mode) {
        case Mode::ECB: return EVP_aes_192_ecb();
        case Mode::CBC: return EVP_aes_192_cbc();
        case Mode::CFB: return EVP_aes_192_cfb();
        case Mode::OFB: return EVP_aes_192_ofb();
        case Mode::GCM: return EVP_aes_192_gcm();
        case Mode::CTR: return EVP_aes_192_ctr();
        }
        break;
    case 256:
        switch (m_mode) {
        case Mode::ECB: return EVP_aes_256_ecb();
        case Mode::CBC: return EVP_aes_256_cbc();
        case Mode::CFB: return EVP_aes_256_cfb();
        case Mode::OFB: return EVP_aes_256_ofb();
        case Mode::GCM: return EVP_aes_256_gcm();
        case Mode::CTR: return EVP_aes_256_ctr();
        }
        break;
    }
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::UNSUPPORTED_KEY_SIZE_OR_MODE;
    return nullptr;
}

// RSAEncryption 实现
RSAEncryption::RSAEncryption(QObject *parent)
    : QObject(parent)
    , m_keySize(2048)
    , m_padding(Padding::PKCS1)
    , m_publicKey(nullptr)
    , m_privateKey(nullptr)
{
}

RSAEncryption::~RSAEncryption()
{
    if (m_publicKey) {
        EVP_PKEY_free(m_publicKey);
    }
    if (m_privateKey) {
        EVP_PKEY_free(m_privateKey);
    }
}

bool RSAEncryption::generateKeyPair()
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_CREATE_KEY_CONTEXT;
        return false;
    }
    
    if (EVP_PKEY_keygen_init(ctx) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_INIT_KEY_GENERATION;
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, m_keySize) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_SET_KEY_SIZE;
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    
    EVP_PKEY *keyPair = nullptr;
    if (EVP_PKEY_keygen(ctx, &keyPair) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_GENERATE_KEY_PAIR;
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    
    // 清理旧密钥
    if (m_publicKey) {
        EVP_PKEY_free(m_publicKey);
    }
    if (m_privateKey) {
        EVP_PKEY_free(m_privateKey);
    }
    
    // 复制密钥
    m_publicKey = keyPair;
    m_privateKey = keyPair;
    EVP_PKEY_up_ref(keyPair); // 增加引用计数
    
    EVP_PKEY_CTX_free(ctx);
    return true;
}

bool RSAEncryption::setPublicKey(const QByteArray &keyData)
{
    BIO *bio = BIO_new_mem_buf(keyData.data(), keyData.size());
    if (!bio) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_CREATE_BIO_PUBLIC;
        return false;
    }
    
    EVP_PKEY *key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    
    if (!key) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_PARSE_PUBLIC_KEY;
        return false;
    }
    
    if (m_publicKey) {
        EVP_PKEY_free(m_publicKey);
    }
    
    m_publicKey = key;
    return true;
}

bool RSAEncryption::setPrivateKey(const QByteArray &keyData, const QString &password)
{
    BIO *bio = BIO_new_mem_buf(keyData.data(), keyData.size());
    if (!bio) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_CREATE_BIO_PRIVATE;
        return false;
    }
    
    const char *pass = password.isEmpty() ? nullptr : password.toUtf8().data();
    EVP_PKEY *key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, const_cast<char*>(pass));
    BIO_free(bio);
    
    if (!key) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_PARSE_PRIVATE_KEY;
        return false;
    }
    
    if (m_privateKey) {
        EVP_PKEY_free(m_privateKey);
    }
    
    m_privateKey = key;
    return true;
}

QByteArray RSAEncryption::getPublicKey() const
{
    if (!m_publicKey) {
        return QByteArray();
    }
    
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) {
        return QByteArray();
    }
    
    if (PEM_write_bio_PUBKEY(bio, m_publicKey) != 1) {
        BIO_free(bio);
        return QByteArray();
    }
    
    char *data;
    long len = BIO_get_mem_data(bio, &data);
    QByteArray result(data, len);
    
    BIO_free(bio);
    return result;
}

QByteArray RSAEncryption::getPrivateKey(const QString &password) const
{
    if (!m_privateKey) {
        return QByteArray();
    }
    
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) {
        return QByteArray();
    }
    
    const EVP_CIPHER *cipher = password.isEmpty() ? nullptr : EVP_aes_256_cbc();
    const char *pass = password.isEmpty() ? nullptr : password.toUtf8().data();
    
    if (PEM_write_bio_PrivateKey(bio, m_privateKey, cipher,
                                nullptr, 0, nullptr, const_cast<char*>(pass)) != 1) {
        BIO_free(bio);
        return QByteArray();
    }
    
    char *data;
    long len = BIO_get_mem_data(bio, &data);
    QByteArray result(data, len);
    
    BIO_free(bio);
    return result;
}

QByteArray RSAEncryption::encrypt(const QByteArray &data) const
{
    if (!m_publicKey) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::NO_PUBLIC_KEY_ENCRYPTION;
        return QByteArray();
    }
    
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(m_publicKey, nullptr);
    if (!ctx) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_CREATE_ENCRYPT_CONTEXT;
        return QByteArray();
    }
    
    if (EVP_PKEY_encrypt_init(ctx) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_INIT_RSA_ENCRYPTION;
        EVP_PKEY_CTX_free(ctx);
        return QByteArray();
    }
    
    // 设置填充模式
    int padding = getPaddingMode();
    if (EVP_PKEY_CTX_set_rsa_padding(ctx, padding) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_SET_PADDING;
        EVP_PKEY_CTX_free(ctx);
        return QByteArray();
    }
    
    size_t outLen;
    if (EVP_PKEY_encrypt(ctx, nullptr, &outLen,
                        reinterpret_cast<const unsigned char*>(data.data()),
                        data.size()) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_DETERMINE_OUTPUT_LENGTH;
        EVP_PKEY_CTX_free(ctx);
        return QByteArray();
    }
    
    QByteArray encrypted(outLen, 0);
    if (EVP_PKEY_encrypt(ctx,
                        reinterpret_cast<unsigned char*>(encrypted.data()),
                        &outLen,
                        reinterpret_cast<const unsigned char*>(data.data()),
                        data.size()) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_RSA_ENCRYPT_DATA;
        EVP_PKEY_CTX_free(ctx);
        return QByteArray();
    }
    
    EVP_PKEY_CTX_free(ctx);
    encrypted.resize(outLen);
    return encrypted;
}

QByteArray RSAEncryption::decrypt(const QByteArray &encryptedData) const
{
    if (!m_privateKey) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::NO_PRIVATE_KEY_DECRYPTION;
        return QByteArray();
    }
    
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(m_privateKey, nullptr);
    if (!ctx) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_CREATE_DECRYPT_CONTEXT;
        return QByteArray();
    }
    
    if (EVP_PKEY_decrypt_init(ctx) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_INIT_RSA_DECRYPTION;
        EVP_PKEY_CTX_free(ctx);
        return QByteArray();
    }
    
    // 设置填充模式
    int padding = getPaddingMode();
    if (EVP_PKEY_CTX_set_rsa_padding(ctx, padding) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << "Failed to set padding mode";
        EVP_PKEY_CTX_free(ctx);
        return QByteArray();
    }
    
    size_t outLen;
    if (EVP_PKEY_decrypt(ctx, nullptr, &outLen,
                        reinterpret_cast<const unsigned char*>(encryptedData.data()),
                        encryptedData.size()) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << "Failed to determine output length";
        EVP_PKEY_CTX_free(ctx);
        return QByteArray();
    }
    
    QByteArray decrypted(outLen, 0);
    if (EVP_PKEY_decrypt(ctx,
                        reinterpret_cast<unsigned char*>(decrypted.data()),
                        &outLen,
                        reinterpret_cast<const unsigned char*>(encryptedData.data()),
                        encryptedData.size()) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_RSA_DECRYPT_DATA;
        EVP_PKEY_CTX_free(ctx);
        return QByteArray();
    }
    
    EVP_PKEY_CTX_free(ctx);
    decrypted.resize(outLen);
    return decrypted;
}

QByteArray RSAEncryption::sign(const QByteArray &data) const
{
    if (!m_privateKey) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::NO_PRIVATE_KEY_SIGNING;
        return QByteArray();
    }
    
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_CREATE_SIGN_CONTEXT;
        return QByteArray();
    }
    
    if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, m_privateKey) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_INIT_SIGNING;
        EVP_MD_CTX_free(ctx);
        return QByteArray();
    }
    
    if (EVP_DigestSignUpdate(ctx, data.data(), data.size()) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_UPDATE_SIGNING;
        EVP_MD_CTX_free(ctx);
        return QByteArray();
    }
    
    size_t sigLen;
    if (EVP_DigestSignFinal(ctx, nullptr, &sigLen) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_DETERMINE_SIGNATURE_LENGTH;
        EVP_MD_CTX_free(ctx);
        return QByteArray();
    }
    
    QByteArray signature(sigLen, 0);
    if (EVP_DigestSignFinal(ctx,
                           reinterpret_cast<unsigned char*>(signature.data()),
                           &sigLen) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_CREATE_SIGNATURE;
        EVP_MD_CTX_free(ctx);
        return QByteArray();
    }
    
    EVP_MD_CTX_free(ctx);
    signature.resize(sigLen);
    return signature;
}

bool RSAEncryption::verify(const QByteArray &data, const QByteArray &signature) const
{
    if (!m_publicKey) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::NO_PUBLIC_KEY_VERIFICATION;
        return false;
    }
    
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_CREATE_VERIFY_CONTEXT;
        return false;
    }
    
    if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, m_publicKey) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_INIT_VERIFICATION;
        EVP_MD_CTX_free(ctx);
        return false;
    }
    
    if (EVP_DigestVerifyUpdate(ctx, data.data(), data.size()) <= 0) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_UPDATE_VERIFICATION;
        EVP_MD_CTX_free(ctx);
        return false;
    }
    
    int result = EVP_DigestVerifyFinal(ctx,
                                      reinterpret_cast<const unsigned char*>(signature.data()),
                                      signature.size());
    
    EVP_MD_CTX_free(ctx);
    return result == 1;
}

int RSAEncryption::getPaddingMode() const
{
    switch (m_padding) {
    case Padding::PKCS1: return RSA_PKCS1_PADDING;
    case Padding::OAEP: return RSA_PKCS1_OAEP_PADDING;
    case Padding::None: return RSA_NO_PADDING;
    default: return RSA_PKCS1_PADDING;
    }
}

// HashGenerator 实现
QByteArray HashGenerator::md5(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Md5);
}

QByteArray HashGenerator::sha1(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha1);
}

QByteArray HashGenerator::sha256(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QByteArray HashGenerator::sha512(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha512);
}

QByteArray HashGenerator::hmacSha256(const QByteArray &data, const QByteArray &key)
{
    unsigned char result[SHA256_DIGEST_LENGTH];
    unsigned int resultLen;
    
    HMAC(EVP_sha256(),
         key.data(), key.size(),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         result, &resultLen);
    
    return QByteArray(reinterpret_cast<const char*>(result), resultLen);
}

QByteArray HashGenerator::pbkdf2(const QByteArray &password, const QByteArray &salt, int iterations, int keyLength)
{
    QByteArray key(keyLength, 0);
    
    if (PKCS5_PBKDF2_HMAC(password.data(), password.size(),
                         reinterpret_cast<const unsigned char*>(salt.data()), salt.size(),
                         iterations, EVP_sha256(),
                         keyLength, reinterpret_cast<unsigned char*>(key.data())) != 1) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::PBKDF2_DERIVATION_FAILED;
        return QByteArray();
    }
    
    return key;
}

// RandomGenerator 实现
QByteArray RandomGenerator::generateBytes(int size)
{
    QByteArray data(size, 0);
    
    if (RAND_bytes(reinterpret_cast<unsigned char*>(data.data()), size) != 1) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcEncryption) << MessageConstants::Encryption::FAILED_GENERATE_RANDOM_BYTES;
        return QByteArray();
    }
    
    return data;
}

int RandomGenerator::generateInt(int min, int max)
{
    if (min >= max) {
        return min;
    }
    
    return QRandomGenerator::global()->bounded(min, max + 1);
}

QString RandomGenerator::generateString(int length, const QString &charset)
{
    QString defaultCharset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    QString actualCharset = charset.isEmpty() ? defaultCharset : charset;
    
    QString result;
    result.reserve(length);
    
    for (int i = 0; i < length; ++i) {
        int index = generateInt(0, actualCharset.length() - 1);
        result.append(actualCharset.at(index));
    }
    
    return result;
}

QByteArray RandomGenerator::generateSalt(int size)
{
    return generateBytes(size);
}