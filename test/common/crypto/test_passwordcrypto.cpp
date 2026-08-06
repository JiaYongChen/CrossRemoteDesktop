#include <QtTest/QTest>
#include "common/crypto/PasswordCrypto.h"

class PasswordCryptoTest : public QObject {
    Q_OBJECT

private slots:
    void encryptDecrypt_roundtrip()
    {
        const QString username = "testuser";
        const QString plainText = "MySecretPassword123";

        const QString encrypted = PasswordCrypto::encrypt(username, plainText);
        QVERIFY(!encrypted.isEmpty());

        const QString decrypted = PasswordCrypto::decrypt(username, encrypted);
        QCOMPARE(decrypted, plainText);
    }

    void encrypt_differentUsername_differentCipher()
    {
        const QString plainText = "SamePassword";

        const QString enc1 = PasswordCrypto::encrypt("userA", plainText);
        const QString enc2 = PasswordCrypto::encrypt("userB", plainText);

        QVERIFY(!enc1.isEmpty());
        QVERIFY(!enc2.isEmpty());
        QVERIFY(enc1 != enc2);
    }

    void decrypt_wrongUsername_returnsEmpty()
    {
        const QString encrypted = PasswordCrypto::encrypt("alice", "secret123");
        QVERIFY(!encrypted.isEmpty());

        const QString result = PasswordCrypto::decrypt("bob", encrypted);
        QVERIFY(result.isEmpty());
    }

    void decrypt_tampered_returnsEmpty()
    {
        const QString encrypted = PasswordCrypto::encrypt("user", "secret123");
        QVERIFY(!encrypted.isEmpty());

        QString tampered = encrypted;
        const int mid = tampered.size() / 2;
        tampered[mid] = (tampered[mid] == 'A') ? 'B' : 'A';

        const QString result = PasswordCrypto::decrypt("user", tampered);
        QVERIFY(result.isEmpty());
    }

    void encrypt_emptyInput_returnsEmpty()
    {
        const QString result = PasswordCrypto::encrypt("user", QString());
        QVERIFY(result.isEmpty());
    }

    void decrypt_emptyInput_returnsEmpty()
    {
        const QString result = PasswordCrypto::decrypt("user", QString());
        QVERIFY(result.isEmpty());
    }
};

QTEST_MAIN(PasswordCryptoTest)
#include "test_passwordcrypto.moc"
