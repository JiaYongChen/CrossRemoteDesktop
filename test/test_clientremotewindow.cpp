#include <QtTest/QtTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QFontMetrics>
#include "../src/client/window/ClientRemoteWindow.h"
#include "../src/client/managers/SessionManager.h"
#include "../src/client/network/ConnectionManager.h"
#include "../src/common/core/config/MessageConstants.h"

/**
 * @brief ClientRemoteWindow 组件的单元测试
 * 
 * 主要验证：
 * 1. 连接状态正确设置和获取
 * 2. 窗口标题根据连接状态动态更新
 * 3. 默认窗口大小设置正确
 */
class TestClientRemoteWindow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testConnectionStateSetGet();
    void testConnectionStateDisplay();
    void testDefaultWindowSize();
    void testWindowTitleUpdate();

private:
    QApplication *m_app = nullptr;
    ClientRemoteWindow *m_window = nullptr;
    QWidget *m_parentWidget = nullptr;
    SessionManager *m_sessionManager = nullptr;
};

void TestClientRemoteWindow::initTestCase()
{
    // 创建 QApplication 实例（如果尚未存在）
    if (!QApplication::instance()) {
        int argc = 0;
        char **argv = nullptr;
        m_app = new QApplication(argc, argv);
    }
}

void TestClientRemoteWindow::cleanupTestCase()
{
    if (m_app) {
        delete m_app;
        m_app = nullptr;
    }
}

void TestClientRemoteWindow::init()
{
    // 为每个测试创建干净的 ClientRemoteWindow 实例
    m_parentWidget = new QWidget();
    
    // 创建 SessionManager（内部会创建 ConnectionManager）
    // 需要提供 connectionId 参数
    QString testConnectionId = "test-connection-id";
    m_sessionManager = new SessionManager(testConnectionId, m_parentWidget);
    
    // 创建 ClientRemoteWindow，传入 SessionManager
    m_window = new ClientRemoteWindow(m_sessionManager, m_parentWidget);
    
    // 设置一个合理的窗口大小以便测试绘制功能
    m_window->resize(800, 600);
    m_parentWidget->resize(800, 600);
}

void TestClientRemoteWindow::cleanup()
{
    if (m_window) {
        delete m_window;
        m_window = nullptr;
    }
    if (m_sessionManager) {
        delete m_sessionManager;
        m_sessionManager = nullptr;
    }
    // ConnectionManager 由 SessionManager 内部管理，不需要单独删除
    if (m_parentWidget) {
        delete m_parentWidget;
        m_parentWidget = nullptr;
    }
}

void TestClientRemoteWindow::testConnectionStateSetGet()
{
    // 测试连接状态的设置和获取
    QVERIFY(m_window != nullptr);
    
    // 初始状态应为 Disconnected
    QCOMPARE(m_window->connectionState(), ConnectionManager::Disconnected);
    
    // 测试设置不同的连接状态
    m_window->setConnectionState(ConnectionManager::Connecting);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Connecting);
    
    m_window->setConnectionState(ConnectionManager::Connected);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Connected);
    
    m_window->setConnectionState(ConnectionManager::Authenticating);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Authenticating);
    
    m_window->setConnectionState(ConnectionManager::Authenticated);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Authenticated);
    
    m_window->setConnectionState(ConnectionManager::Disconnecting);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Disconnecting);
    
    m_window->setConnectionState(ConnectionManager::Reconnecting);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Reconnecting);
    
    m_window->setConnectionState(ConnectionManager::Error);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Error);
    
    m_window->setConnectionState(ConnectionManager::Disconnected);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Disconnected);
}

void TestClientRemoteWindow::testConnectionStateDisplay()
{
    // 综合测试：验证连接状态显示的完整流程
    QVERIFY(m_window != nullptr);
    
    // 模拟连接过程：Disconnected -> Connecting -> Connected -> Authenticated
    m_window->setConnectionState(ConnectionManager::Disconnected);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Disconnected);
    
    m_window->setConnectionState(ConnectionManager::Connecting);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Connecting);
    
    m_window->setConnectionState(ConnectionManager::Connected);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Connected);
    
    m_window->setConnectionState(ConnectionManager::Authenticated);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Authenticated);
    
    // 模拟断开过程：Authenticated -> Disconnecting -> Disconnected
    m_window->setConnectionState(ConnectionManager::Disconnecting);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Disconnecting);
    
    m_window->setConnectionState(ConnectionManager::Disconnected);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Disconnected);
    
    // 模拟错误状态
    m_window->setConnectionState(ConnectionManager::Error);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Error);
    
    // 模拟重连过程
    m_window->setConnectionState(ConnectionManager::Reconnecting);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Reconnecting);
    
    m_window->setConnectionState(ConnectionManager::Connected);
    QCOMPARE(m_window->connectionState(), ConnectionManager::Connected);
}

void TestClientRemoteWindow::testDefaultWindowSize()
{
    // 测试默认窗口大小是否为 1600x900 (16:9 宽高比)
    // 注意：由于在 init() 中我们手动设置了窗口大小为 800x600，
    // 我们需要创建一个新的窗口实例来测试默认大小
    
    QWidget *testParent = new QWidget();
    QString testConnectionId = "test-window-size-connection-id";
    SessionManager *testSessionManager = new SessionManager(testConnectionId, testParent);
    ClientRemoteWindow *testWindow = new ClientRemoteWindow(testSessionManager, testParent);
    
    // 验证默认窗口大小 (1600x900, 符合现代显示器的 16:9 比例)
    QSize expectedSize(1600, 900);
    QSize actualSize = testWindow->size();
    
    QCOMPARE(actualSize.width(), expectedSize.width());
    QCOMPARE(actualSize.height(), expectedSize.height());
    
    // 验证最小窗口大小 (400x225, 保持 16:9 比例)
    QSize expectedMinSize(400, 225);
    QSize actualMinSize = testWindow->minimumSize();
    
    QCOMPARE(actualMinSize.width(), expectedMinSize.width());
    QCOMPARE(actualMinSize.height(), expectedMinSize.height());
    
    // 清理测试对象
    delete testWindow;
    delete testSessionManager;
    // ConnectionManager 由 SessionManager 内部管理
    delete testParent;
}

void TestClientRemoteWindow::testWindowTitleUpdate()
{
    // 测试窗口标题根据连接状态自动更新
    QVERIFY(m_window != nullptr);
    
    // 设置一个主机名用于测试
    QString testHost = "192.168.1.100";
    m_window->updateWindowTitle(testHost);
    
    // 验证窗口标题包含主机名
    QVERIFY(m_window->windowTitle().contains(testHost));
    
    // 测试不同连接状态下的窗口标题
    // 注意：由于 updateWindowTitle() 在 setConnectionState() 中自动调用
    // 我们需要先设置主机名，然后改变状态
    
    // Connecting 状态
    m_window->setConnectionState(ConnectionManager::Connecting);
    QString title = m_window->windowTitle();
    // 标题应该包含主机名和状态信息
    QVERIFY(!title.isEmpty());
    
    // Connected 状态
    m_window->setConnectionState(ConnectionManager::Connected);
    title = m_window->windowTitle();
    QVERIFY(!title.isEmpty());
    
    // Authenticated 状态
    m_window->setConnectionState(ConnectionManager::Authenticated);
    title = m_window->windowTitle();
    QVERIFY(!title.isEmpty());
    
    // Error 状态
    m_window->setConnectionState(ConnectionManager::Error);
    title = m_window->windowTitle();
    QVERIFY(!title.isEmpty());
}

QTEST_MAIN(TestClientRemoteWindow)
#include "test_clientremotewindow.moc"