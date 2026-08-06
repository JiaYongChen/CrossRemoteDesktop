// 测试 SettingsDialog：端口/语言/恢复默认值的即时生效持久化
//
// 通过 findChild 按 objectName 访问 .ui 控件，直接驱动控件信号验证
// 配置写入 SettingsManager（内存即时生效，500ms 去抖后才落盘）。
// 配置数据使用 QTemporaryFile 隔离，不污染真实 config.json。

#include <QtCore/QDir>
#include <QtCore/QTemporaryFile>
#include <QtTest/QtTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>

#include "app/SettingsDialog.h"
#include "common/config/NetworkConstants.h"
#include "common/config/SettingsManager.h"
#include "common/platform/AutoStartManager.h"

class SettingsDialogTest : public QObject {
    Q_OBJECT

private:
    QApplication* m_app = nullptr;
    SettingsManager* m_settings = nullptr;
    AutoStartManager* m_autoStart = nullptr;
    SettingsDialog* m_dlg = nullptr;
    QTemporaryFile* m_tempFile = nullptr;

private slots:
    void initTestCase()
    {
        if (!QApplication::instance()) {
            int argc = 0;
            char** argv = nullptr;
            m_app = new QApplication(argc, argv);
        }
    }

    void init()
    {
        m_tempFile = new QTemporaryFile(QDir::tempPath() + "/rd_test_XXXXXX.json");
        QVERIFY(m_tempFile->open());
        m_tempFile->close();

        m_settings = new SettingsManager(m_tempFile->fileName(), this);
        m_settings->setInt("Server/listenPort", NetworkConstants::DefaultServerPort);

        m_autoStart = new AutoStartManager(this);
        m_dlg = new SettingsDialog(m_settings, m_autoStart);
    }

    void cleanup()
    {
        delete m_dlg;
        m_dlg = nullptr;
        delete m_autoStart;
        m_autoStart = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_tempFile;
        m_tempFile = nullptr;
    }

    void portChange_persistsToSettings()
    {
        auto* portSpin = m_dlg->findChild<QSpinBox*>("listenPortSpinBox");
        QVERIFY(portSpin != nullptr);

        portSpin->setValue(12345);

        const int persisted = m_settings->getInt("Server/listenPort");
        QCOMPARE(persisted, 12345);
    }

    void restoreDefaults_resetsToDefaults()
    {
        auto* portSpin = m_dlg->findChild<QSpinBox*>("listenPortSpinBox");
        QVERIFY(portSpin != nullptr);
        portSpin->setValue(9999);

        auto* closeToTray = m_dlg->findChild<QCheckBox*>("closeToTrayCheckBox");
        QVERIFY(closeToTray != nullptr);
        closeToTray->setChecked(true);

        QMetaObject::invokeMethod(m_dlg, "onRestoreDefaultsClicked");

        const int port = m_settings->getInt("Server/listenPort");
        QCOMPARE(port, NetworkConstants::DefaultServerPort);

        const bool ctt = m_settings->getBool("UI/closeToTray");
        QCOMPARE(ctt, false);
    }

    void languageChange_persists()
    {
        auto* langCombo = m_dlg->findChild<QComboBox*>("languageComboBox");
        QVERIFY(langCombo != nullptr);

        langCombo->setCurrentIndex(1);

        const QString lang = m_settings->getString("General/language");
        QCOMPARE(lang, QString("en_US"));
    }
};

QTEST_MAIN(SettingsDialogTest)
#include "test_settingsdialog.moc"
