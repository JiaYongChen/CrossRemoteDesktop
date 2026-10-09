#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QLibraryInfo>
#include <QtTest/QtTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QPushButton>

#include "app/ConnectionDialog.h"
#include "common/config/TranslationUtils.h"

class TranslationUtilsTest : public QObject {
    Q_OBJECT

private slots:
    void deployedTranslations_switchBothApplicationAndStandardButtons()
    {
        // qt.conf 将 Qt 安装翻译目录指向不存在的路径；本测试也没有 RD_TRANSLATIONS_DIR。
        QVERIFY(!QDir(QLibraryInfo::path(QLibraryInfo::TranslationsPath)).exists());
        QCOMPARE(switchTranslation(*qApp, QStringLiteral("zh_CN")), QStringLiteral("zh_CN"));

        ConnectionDialog dialog;
        auto* buttons = dialog.findChild<QDialogButtonBox*>("buttonBox");
        QVERIFY(buttons != nullptr);
        QDialogButtonBox standardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Yes);
        auto* cancel = buttons->button(QDialogButtonBox::Cancel);
        auto* connect = buttons->button(QDialogButtonBox::Ok);
        QVERIFY(cancel != nullptr);
        QVERIFY(connect != nullptr);
        QCOMPARE(cancel->text(), QStringLiteral("取消"));
        QCOMPARE(connect->text(), QStringLiteral("连接"));
        QVERIFY(standardButtons.button(QDialogButtonBox::Cancel)->text().contains(QStringLiteral("取消")));
        QVERIFY(standardButtons.button(QDialogButtonBox::Yes)->text().contains(QStringLiteral("是")));

        QCOMPARE(switchTranslation(*qApp, QStringLiteral("en_US")), QStringLiteral("en_US"));
        QCoreApplication::processEvents();
        QCOMPARE(cancel->text(), QStringLiteral("Cancel"));
        QCOMPARE(connect->text(), QStringLiteral("Connect"));
        QVERIFY(standardButtons.button(QDialogButtonBox::Cancel)->text().contains(QStringLiteral("Cancel")));
        QVERIFY(standardButtons.button(QDialogButtonBox::Yes)->text().contains(QStringLiteral("Yes")));

        QCOMPARE(switchTranslation(*qApp, QStringLiteral("zh_CN")), QStringLiteral("zh_CN"));
        QCoreApplication::processEvents();
        QCOMPARE(cancel->text(), QStringLiteral("取消"));
        QCOMPARE(connect->text(), QStringLiteral("连接"));
        QVERIFY(standardButtons.button(QDialogButtonBox::Cancel)->text().contains(QStringLiteral("取消")));
        QVERIFY(standardButtons.button(QDialogButtonBox::Yes)->text().contains(QStringLiteral("是")));
    }
};

QTEST_MAIN(TranslationUtilsTest)
#include "test_translationutils.moc"
