#include <QtTest/QTest>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>

#include "../src/common/core/config/RenderConfig.h"

class TestRenderConfig : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_tmp;
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("QrdTest");
        QCoreApplication::setApplicationName("RenderConfigStandalone");
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_tmp.path());
        // CRITICAL: Must set default format to IniFormat so QSettings()
        // (used in RenderConfig::load()) reads from the same format.
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }
    void testDefaultsVSyncOnAndPboOn() {
        // Fresh scope, no prior values.
        QSettings s;
        s.clear();
        s.sync();
        const auto cfg = RenderConfig::load();
        QVERIFY(cfg.gl.vsyncEnabled);
        QVERIFY(cfg.gl.usePbo);
    }
    void testVSyncCanBeDisabled() {
        QSettings s;
        s.beginGroup("RemoteDesktop/Render");
        s.setValue("VSync", false);
        s.endGroup();
        s.sync();
        const auto cfg = RenderConfig::load();
        QVERIFY(!cfg.gl.vsyncEnabled);
    }
};

QTEST_MAIN(TestRenderConfig)
#include "test_render_config.moc"
