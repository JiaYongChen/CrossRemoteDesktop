#pragma once

#include <QtCore/QString>
#include <QtCore/QByteArray>
#include "../config/SecurityConstants.h"

/**
 * @brief 密码加解密工具类
 *
 * 使用 AES-256-CBC 加密密码，Base64 编码后存储到 QSettings。
 * 加密密钥由用户名 + 固定盐通过 SHA-256 派生。
 */
class PasswordCrypto {
public:
    static QString encrypt(const QString& username, const QString& plainText);
    static QString decrypt(const QString& username, const QString& cipherText);

private:
    static QByteArray deriveKey(const QString& username);
    static QByteArray generateIV();
};
