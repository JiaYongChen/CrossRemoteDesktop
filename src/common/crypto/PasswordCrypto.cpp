#include "PasswordCrypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "common/config/SecurityConstants.h"

namespace {

constexpr unsigned char FIXED_SALT[32] = {
    0x7a, 0x3b, 0x91, 0xd4, 0x2e, 0x88, 0x5f, 0x13,
    0xc6, 0xaa, 0x4d, 0x71, 0x0f, 0xbe, 0x52, 0x39,
    0x68, 0x1c, 0xfd, 0x97, 0x24, 0x85, 0xeb, 0x46,
    0x30, 0x9a, 0x57, 0xdc, 0x81, 0x6e, 0xa3, 0x05
};

const QByteArray& saltBytes() {
    static const QByteArray s(reinterpret_cast<const char*>(FIXED_SALT), 32);
    return s;
}

} // anonymous namespace

QByteArray PasswordCrypto::deriveKey(const QString& username)
{
    const QByteArray input = (username.isEmpty()
                              ? QByteArray(SecurityConstants::DefaultCryptoUsername)
                              : username.toUtf8()) + saltBytes();

    QByteArray hash(SHA256_DIGEST_LENGTH, '\0');
    SHA256(reinterpret_cast<const unsigned char*>(input.constData()),
           static_cast<size_t>(input.size()),
           reinterpret_cast<unsigned char*>(hash.data()));
    return hash;
}

QByteArray PasswordCrypto::generateIV()
{
    QByteArray iv(SecurityConstants::IvSize, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), SecurityConstants::IvSize) != 1) {
        return {};
    }
    return iv;
}

QString PasswordCrypto::encrypt(const QString& username, const QString& plainText)
{
    if (plainText.isEmpty()) {
        return {};
    }

    const QByteArray key = deriveKey(username);
    const QByteArray iv = generateIV();
    if (iv.isEmpty()) {
        return {};
    }

    const QByteArray plainBytes = plainText.toUtf8();

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return {};
    }

    struct CtxGuard { EVP_CIPHER_CTX* c; ~CtxGuard() { if (c) EVP_CIPHER_CTX_free(c); } } guard{ctx};

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1) {
        return {};
    }

    QByteArray cipherBytes(plainBytes.size() + EVP_CIPHER_CTX_block_size(ctx), '\0');
    int outLen = 0;

    if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(cipherBytes.data()), &outLen,
                          reinterpret_cast<const unsigned char*>(plainBytes.constData()),
                          plainBytes.size()) != 1) {
        return {};
    }

    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(cipherBytes.data()) + outLen, &finalLen) != 1) {
        return {};
    }

    cipherBytes.resize(outLen + finalLen);

    const QByteArray combined = iv + cipherBytes;
    return QString::fromLatin1(combined.toBase64());
}

QString PasswordCrypto::decrypt(const QString& username, const QString& cipherText)
{
    if (cipherText.isEmpty()) {
        return {};
    }

    const QByteArray key = deriveKey(username);
    const QByteArray combined = QByteArray::fromBase64(cipherText.toLatin1());

    if (combined.size() <= SecurityConstants::IvSize) {
        return {};
    }

    const QByteArray iv = combined.left(SecurityConstants::IvSize);
    const QByteArray encrypted = combined.mid(SecurityConstants::IvSize);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return {};
    }

    struct CtxGuard { EVP_CIPHER_CTX* c; ~CtxGuard() { if (c) EVP_CIPHER_CTX_free(c); } } guard{ctx};

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1) {
        return {};
    }

    QByteArray plainBytes(encrypted.size(), '\0');
    int outLen = 0;

    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plainBytes.data()), &outLen,
                          reinterpret_cast<const unsigned char*>(encrypted.constData()),
                          encrypted.size()) != 1) {
        return {};
    }

    int finalLen = 0;
    if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plainBytes.data()) + outLen, &finalLen) != 1) {
        return {};
    }

    plainBytes.resize(outLen + finalLen);
    return QString::fromUtf8(plainBytes);
}
