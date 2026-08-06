#include <QtCore/QDebug>
#include <QtCore/QMetaType>
#include <QtTest/QTest>

#include "common/error/RdError.h"

class RdErrorTest : public QObject {
    Q_OBJECT

private slots:
    void construct_default_hasUnknownCode()
    {
        RdError err;
        QCOMPARE(err.code, ErrorCode::Unknown);
        QVERIFY(err.message.isEmpty());
        QVERIFY(err.source.isEmpty());
        QCOMPARE(err.timestampMs, 0);
    }

    void construct_withParams_setsAllFields()
    {
        RdError err(ErrorCode::NetworkConnectionFailed,
                    "connection refused",
                    "TcpClient");
        QCOMPARE(err.code, ErrorCode::NetworkConnectionFailed);
        QCOMPARE(err.message, QString("connection refused"));
        QCOMPARE(err.source, QString("TcpClient"));
        QVERIFY(err.timestampMs > 0);  // 构造时自动获取当前时间戳
    }

    void logLabel_withSource_prependsSource()
    {
        RdError err(ErrorCode::ThreadStartFailed,
                    "worker thread failed to start",
                    "ThreadManager");
        const QString label = err.logLabel();
        QCOMPARE(label, QString("ThreadManager: worker thread failed to start"));
    }

    void logLabel_withoutSource_usesMessageOnly()
    {
        RdError err(ErrorCode::Unknown, "something went wrong");
        const QString label = err.logLabel();
        QCOMPARE(label, QString("something went wrong"));
    }

    void qDebug_output_containsLogLabel()
    {
        RdError err(ErrorCode::CaptureStartFailed,
                    "DXGI init error",
                    "DxgiCapture");

        QString output;
        QDebug dbg(&output);
        dbg << err;

        QVERIFY(output.contains("DxgiCapture: DXGI init error"));
    }

    void metatype_registered()
    {
        // Q_DECLARE_METATYPE 确保类型可在 QueuedConnection 中传递
        const int id = qMetaTypeId<RdError>();
        QVERIFY(id != QMetaType::UnknownType);
    }
};

QTEST_MAIN(RdErrorTest)
#include "test_rderror.moc"
