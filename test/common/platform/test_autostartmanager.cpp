#include <QtCore/QSettings>
#include <QtTest/QtTest>

#include "common/platform/AutoStartManager.h"

class AutoStartManagerTest : public QObject {
    Q_OBJECT

private:
    AutoStartManager* m_mgr = nullptr;
    bool m_originalState = false;
#ifdef Q_OS_WIN
    QString m_originalRunValue;  // Windows 原始注册表值（用于值级恢复）
#endif

private slots:
    void init()
    {
        m_mgr = new AutoStartManager(this);
        // 保存测试前的原始注册状态
        m_originalState = m_mgr->isAutoStartEnabled();
#ifdef Q_OS_WIN
        // 值级保存：记录原始注册表值，cleanup 精确写回（避免覆盖为测试 exe 路径）
        QSettings runKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                         QSettings::NativeFormat);
        m_originalRunValue = runKey.value(QStringLiteral("CrossRemoteDesktop")).toString();
#endif
    }

    void cleanup()
    {
#ifdef Q_OS_WIN
        // 值级恢复：写回原始注册表值（若原为空则删除）
        QSettings runKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                         QSettings::NativeFormat);
        if (m_originalRunValue.isEmpty()) {
            runKey.remove(QStringLiteral("CrossRemoteDesktop"));
        } else {
            runKey.setValue(QStringLiteral("CrossRemoteDesktop"), m_originalRunValue);
        }
        runKey.sync();
#else
        // 布尔级恢复（幂等：原已启用则保持，原未启用则注销）
        if (m_originalState) {
            Q_UNUSED(m_mgr->setAutoStart(true));
        } else {
            Q_UNUSED(m_mgr->setAutoStart(false));
        }
#endif
        // 注意：不 delete m_mgr（Qt parent-child 所有权）
    }

    void enable_returnsTrue()
    {
        const bool ok = m_mgr->setAutoStart(true);
        QVERIFY(ok);
    }

    void disable_returnsTrue()
    {
        QVERIFY(m_mgr->setAutoStart(true));  // 先启用确保有可禁用的项
        const bool ok = m_mgr->setAutoStart(false);
        QVERIFY(ok);
    }

    void roundtrip_enableThenCheck()
    {
        QVERIFY(m_mgr->setAutoStart(true));
        QVERIFY(m_mgr->isAutoStartEnabled());
    }

    void roundtrip_disableThenCheck()
    {
        QVERIFY(m_mgr->setAutoStart(true));
        QVERIFY(m_mgr->setAutoStart(false));
        QVERIFY(!m_mgr->isAutoStartEnabled());
    }

    void lastError_emptyInitially()
    {
        QVERIFY(m_mgr->lastError().isEmpty());
    }

    void doubleDisable_isIdempotent()
    {
        // 确保处于已禁用状态
        QVERIFY(m_mgr->setAutoStart(false));
        // 再次禁用不应失败（已是禁用状态）
        const bool ok = m_mgr->setAutoStart(false);
        QVERIFY(ok);
    }
};

QTEST_MAIN(AutoStartManagerTest)
#include "test_autostartmanager.moc"
