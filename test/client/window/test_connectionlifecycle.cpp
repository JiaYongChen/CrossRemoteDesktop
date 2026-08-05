#include <QtTest>
#include <QtWidgets/QDialog>
#include <QtWidgets/QWidget>
#include <QSignalSpy>

#include "client/window/ConnectionLifecycle.h"

/// ConnectionLifecycle 信任对话框生命周期：
/// 状态离开 Connecting（挂起期）时对话框必须被关闭（不得滞留于健康会话之上），
/// 挂起期拒绝关闭窗口，挂起期接受放行且不关窗
class ConnectionLifecycleTest : public QObject {
    Q_OBJECT
private slots:
    void trustDialogDismissedWhenStateLeavesConnecting();
    void rejectWhilePendingClosesWindow();
    void acceptWhilePendingProceedsWithoutClosing();
};

void ConnectionLifecycleTest::trustDialogDismissedWhenStateLeavesConnecting() {
    ConnectionLifecycle lc;
    QWidget window;
    lc.manage(&window);
    lc.setHostName(QStringLiteral("host"));
    window.show();

    lc.onConnecting();   // 进入挂起期（信任验证期间状态保持 Connecting）
    lc.showTrustWarning(QStringLiteral("h:1"), QStringLiteral("fpOld"), QStringLiteral("fpNew"));
    QVERIFY(window.findChild<QDialog*>() != nullptr);

    // 连接转入重连/恢复 → 状态离开挂起态 → 陈旧 MITM 警告必须被关闭，
    // 否则滞留对话框上的过期「取消」点击会关掉已恢复的健康窗口
    lc.onReconnecting();
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QDialog*>() == nullptr, 1000);
    QVERIFY(window.isVisible());   // 窗口健康，不得被连带关闭
}

void ConnectionLifecycleTest::rejectWhilePendingClosesWindow() {
    ConnectionLifecycle lc;
    QWidget window;
    lc.manage(&window);
    lc.setHostName(QStringLiteral("host"));
    window.show();

    lc.onConnecting();   // 挂起期
    QSignalSpy decisions(&lc, &ConnectionLifecycle::trustDecision);
    lc.showTrustWarning(QStringLiteral("h:1"), QStringLiteral("fpOld"), QStringLiteral("fpNew"));
    auto* dialog = window.findChild<QDialog*>();
    QVERIFY(dialog != nullptr);

    dialog->reject();   // 用户点击「取消」（决策有效：仍处挂起态）

    QCOMPARE(decisions.count(), 1);
    QCOMPARE(decisions.at(0).at(0).toBool(), false);
    QTRY_VERIFY_WITH_TIMEOUT(window.isHidden(), 1000);   // 拒绝生效 → 关闭窗口
}

void ConnectionLifecycleTest::acceptWhilePendingProceedsWithoutClosing() {
    ConnectionLifecycle lc;
    QWidget window;
    lc.manage(&window);
    lc.setHostName(QStringLiteral("host"));
    window.show();

    lc.onConnecting();   // 挂起期
    QSignalSpy decisions(&lc, &ConnectionLifecycle::trustDecision);
    lc.showTrustWarning(QStringLiteral("h:1"), QStringLiteral("fpOld"), QStringLiteral("fpNew"));
    auto* dialog = window.findChild<QDialog*>();
    QVERIFY(dialog != nullptr);

    dialog->accept();   // 用户点击「信任」（决策有效：仍处挂起态）

    QCOMPARE(decisions.count(), 1);
    QCOMPARE(decisions.at(0).at(0).toBool(), true);
    QTest::qWait(50);
    QVERIFY(window.isVisible());   // 接受 → 继续连接，窗口不得关闭
}

QTEST_MAIN(ConnectionLifecycleTest)
#include "test_connectionlifecycle.moc"
