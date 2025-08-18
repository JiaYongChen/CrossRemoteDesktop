#ifndef ICRYPTO_H
#define ICRYPTO_H

#include <QtCore/QByteArray>

// 加密接口：阶段A仅定义接口，后续阶段实现密钥协商与AES-GCM。
class ICrypto {
public:
    virtual ~ICrypto() = default;

    // 设置或更新会话密钥/IV等（格式与含义由实现定义）
    virtual void setKey(const QByteArray& key, const QByteArray& iv = QByteArray()) = 0;

    virtual QByteArray encrypt(const QByteArray& plaintext, QByteArray* authTag = nullptr) = 0;
    virtual QByteArray decrypt(const QByteArray& ciphertext, const QByteArray& authTag = QByteArray()) = 0;
};

#endif // ICRYPTO_H
