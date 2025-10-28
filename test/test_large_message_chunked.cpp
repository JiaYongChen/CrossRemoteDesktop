#include <QtTest/QtTest>
#include "../src/common/core/network/Protocol.h"
#include "../src/common/model/ScreenData.h"

class LargeMessageChunkedTest : public QObject {
    Q_OBJECT

private slots:
    void testChunkedReceive() {
        // 创建一个大的 ScreenData (8MB)
        const int width = 1920;
        const int height = 1080;
        const int bytesPerPixel = 4; // RGB32
        const int imageSize = width * height * bytesPerPixel;

        QByteArray imageData(imageSize, 0);
        // 填充测试数据
        for ( int i = 0; i < imageSize; i++ ) {
            imageData[i] = static_cast<char>(i % 256);
        }

        ScreenData screenData;
        screenData.setX(0);
        screenData.setY(0);
        screenData.setWidth(width);
        screenData.setHeight(height);
        screenData.setImageData(imageData);
        screenData.setTimestamp(QDateTime::currentMSecsSinceEpoch());

        // 创建加密消息
        QByteArray encryptedMessage = Protocol::createMessage(MessageType::ScreenData, screenData);

        qDebug() << "Created large message:" << encryptedMessage.size() << "bytes";
        QVERIFY(encryptedMessage.size() > 1024 * 1024); // 应该大于 1MB

        // 模拟分块接收 (64KB chunks)
        const int CHUNK_SIZE = 64 * 1024;
        QByteArray receiveBuffer;
        int totalReceived = 0;
        int parseSuccessCount = 0;

        for ( int offset = 0; offset < encryptedMessage.size(); offset += CHUNK_SIZE ) {
            int chunkSize = qMin(CHUNK_SIZE, encryptedMessage.size() - offset);
            QByteArray chunk = encryptedMessage.mid(offset, chunkSize);

            receiveBuffer.append(chunk);
            totalReceived += chunkSize;

            qDebug() << "Received chunk" << (offset / CHUNK_SIZE + 1)
                << "size:" << chunkSize
                << "total:" << totalReceived
                << "buffer:" << receiveBuffer.size();

            // 尝试解析
            MessageHeader header;
            QByteArray payload;
            bool parseOk = Protocol::parseMessage(receiveBuffer, header, payload);

            if ( parseOk ) {
                parseSuccessCount++;
                qDebug() << "Parse SUCCESS at chunk" << (offset / CHUNK_SIZE + 1);

                // 验证消息完整性
                QCOMPARE(header.type, MessageType::ScreenData);

                // 解码 ScreenData
                ScreenData receivedData;
                QVERIFY(receivedData.decode(payload));

                // 验证数据
                QCOMPARE(receivedData.getWidth(), width);
                QCOMPARE(receivedData.getHeight(), height);
                QCOMPARE(receivedData.getImageData().size(), imageSize);
                QCOMPARE(receivedData.getImageData(), imageData);

                // 移除已处理的消息
                qsizetype consumed = static_cast<qsizetype>(SERIALIZED_HEADER_SIZE) + header.length;
                receiveBuffer.remove(0, consumed);

                qDebug() << "Removed" << consumed << "bytes, remaining:" << receiveBuffer.size();
            } else {
                qDebug() << "Parse PENDING at chunk" << (offset / CHUNK_SIZE + 1) << "(waiting for more data)";
            }
        }

        // 验证：应该只成功解析一次（在最后一个chunk到达后）
        QCOMPARE(parseSuccessCount, 1);

        // 验证：所有数据都应该被处理
        QCOMPARE(receiveBuffer.size(), 0);
    }

    void testMultipleSmallMessagesInOneChunk() {
        // 创建多个小消息
        QList<QByteArray> messages;
        for ( int i = 0; i < 10; i++ ) {
            ScreenData screenData;
            screenData.setX(i);
            screenData.setY(i);
            screenData.setWidth(100);
            screenData.setHeight(100);
            QByteArray imageData(100 * 100 * 4, static_cast<char>(i));
            screenData.setImageData(imageData);
            screenData.setTimestamp(QDateTime::currentMSecsSinceEpoch());

            messages.append(Protocol::createMessage(MessageType::ScreenData, screenData));
        }

        // 合并所有消息到一个大buffer（模拟粘包）
        QByteArray receiveBuffer;
        for ( const auto& msg : messages ) {
            receiveBuffer.append(msg);
        }

        qDebug() << "Merged" << messages.size() << "messages into" << receiveBuffer.size() << "bytes";

        // 解析所有消息
        int parseCount = 0;
        while ( receiveBuffer.size() >= static_cast<qsizetype>(SERIALIZED_HEADER_SIZE) ) {
            MessageHeader header;
            QByteArray payload;
            bool parseOk = Protocol::parseMessage(receiveBuffer, header, payload);

            if ( !parseOk ) {
                break;
            }

            parseCount++;

            // 验证消息
            QCOMPARE(header.type, MessageType::ScreenData);

            ScreenData receivedData;
            QVERIFY(receivedData.decode(payload));
            QCOMPARE(receivedData.getX(), parseCount - 1);
            QCOMPARE(receivedData.getY(), parseCount - 1);

            // 移除已处理的消息
            qsizetype consumed = static_cast<qsizetype>(SERIALIZED_HEADER_SIZE) + header.length;
            receiveBuffer.remove(0, consumed);
        }

        // 验证：应该成功解析所有消息
        QCOMPARE(parseCount, messages.size());
        QCOMPARE(receiveBuffer.size(), 0);
    }
};

QTEST_MAIN(LargeMessageChunkedTest)
#include "test_large_message_chunked.moc"
