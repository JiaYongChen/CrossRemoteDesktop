#include <QtCore/QFile>
#include <QtCore/QMimeData>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#include "client/window/DragDropHandler.h"

// 暴露 protected eventFilter 供测试直接驱动（绕过 sendEvent 的平台差异）
class TestDragDropHandler : public DragDropHandler {
public:
    using DragDropHandler::DragDropHandler;
    using DragDropHandler::eventFilter;
};

class DragDropHandlerTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tempDir;

private slots:
    void initTestCase() {
        QVERIFY(m_tempDir.isValid());
    }

    void testDragEnterAcceptsFileUrls() {
        QWidget viewport;
        TestDragDropHandler handler(&viewport);

        QMimeData mimeData;
        mimeData.setUrls({QUrl::fromLocalFile(m_tempDir.path() + "/a.txt")});

        QDragEnterEvent event(QPoint(10, 10), Qt::CopyAction, &mimeData,
                              Qt::LeftButton, Qt::NoModifier);
        QVERIFY(handler.eventFilter(&viewport, &event));
        QVERIFY(event.isAccepted());
    }

    void testDragEnterIgnoresTextOnly() {
        QWidget viewport;
        TestDragDropHandler handler(&viewport);

        QMimeData mimeData;
        mimeData.setText("plain text");

        QDragEnterEvent event(QPoint(10, 10), Qt::CopyAction, &mimeData,
                              Qt::LeftButton, Qt::NoModifier);
        QVERIFY(handler.eventFilter(&viewport, &event));
        QVERIFY(!event.isAccepted());
    }

    void testDropEmitsFilesDroppedToRemote() {
        QWidget viewport;
        TestDragDropHandler handler(&viewport);
        QSignalSpy spy(&handler, &DragDropHandler::filesDroppedToRemote);

        // 创建本地文件供提取
        QFile file(m_tempDir.path() + "/drop.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(100, 'X'));
        file.close();

        QMimeData mimeData;
        mimeData.setUrls({QUrl::fromLocalFile(m_tempDir.path() + "/drop.txt")});

        QDropEvent event(QPointF(20, 20), Qt::CopyAction, &mimeData,
                         Qt::LeftButton, Qt::NoModifier);
        QVERIFY(handler.eventFilter(&viewport, &event));

        QCOMPARE(spy.count(), 1);
        const ClipboardFileList list = qvariant_cast<ClipboardFileList>(spy.at(0).at(0));
        QCOMPARE(list.files.size(), 1);
        QCOMPARE(list.files.at(0).fileName, QString("drop.txt"));
        QCOMPARE(list.files.at(0).fileSize, static_cast<quint64>(100));
    }

    void testDropIgnoresNonLocalUrls() {
        QWidget viewport;
        TestDragDropHandler handler(&viewport);
        QSignalSpy spy(&handler, &DragDropHandler::filesDroppedToRemote);

        QMimeData mimeData;
        mimeData.setUrls({QUrl("https://example.com/remote.txt")});

        QDropEvent event(QPointF(20, 20), Qt::CopyAction, &mimeData,
                         Qt::LeftButton, Qt::NoModifier);
        QVERIFY(handler.eventFilter(&viewport, &event));

        // 非本地 URL 不发射信号
        QCOMPARE(spy.count(), 0);
    }

    void testExtractFilesSkipsMissing() {
        // 不存在的本地文件 + 非本地 URL 均被跳过
        QList<QUrl> urls = {
            QUrl::fromLocalFile(m_tempDir.path() + "/missing.txt"),
            QUrl("https://example.com/remote.txt")
        };
        const ClipboardFileList list = DragDropHandler::extractFiles(urls);
        QCOMPARE(list.files.size(), 0);
    }
};

QTEST_MAIN(DragDropHandlerTest)
#include "test_drag_drop_handler.moc"
