#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QtWidgets/QDialog>
#include <QtCore/QSettings>
#include <QtCore/QDateTime>
#include <QtCore/QMetaType>

class TcpClient;
class QProgressDialog;
class QTimer;
class QTabWidget;
class QWidget;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QPushButton;
class QLabel;

QT_BEGIN_NAMESPACE
namespace Ui { class ConnectionDialog; }
QT_END_NAMESPACE

class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    // 连接信息结构体
    struct ConnectionInfo {
        QString name;
        QString host;
        int port;
        QString username;
        QString displayName;
        QDateTime lastUsed;
        int useCount;
        
        bool operator==(const ConnectionInfo &other) const {
            return host == other.host && port == other.port && username == other.username;
        }
        
        bool operator<(const ConnectionInfo &other) const {
            return lastUsed > other.lastUsed; // 按最后使用时间降序排列
        }
    };
    
    explicit ConnectionDialog(QWidget *parent = nullptr);
    ~ConnectionDialog();
    
    // 获取连接信息
    QString getHost() const;
    int getPort() const;
    QString getUsername() const;
    QString getPassword() const;
    
    // 获取连接选项
    bool getFullScreen() const;
    int getColorDepth() const;
    int getCompressionLevel() const;
    bool getShareClipboard() const;
    bool getShareAudio() const;
    
    // 设置连接信息
    void setHost(const QString &host);
    void setPort(int port);
    void setUsername(const QString &username);
    void setPassword(const QString &password);
    

    
protected:
    void accept() override;
    void reject() override;
    
private slots:
    void onConnectClicked();
    void onCancelClicked();
    void onTestConnectionClicked();

    void onAdvancedToggled(bool show);
    void onHostChanged();
    void onPortChanged();
    void validateInput();
    
    // 测试连接相关槽函数
    void onTestConnected();
    void onTestDisconnected();
    void onTestError(const QString &error);
    void onTestTimeout();
    
private:
    void setupUI();
    void setupConnections();
    void setupValidation();
    void loadSettings();
    void saveSettings();
    
    void createBasicTab();
    void createAdvancedTab();

    
    void updateHistoryList();
    void selectHistoryItem(int index);
    bool validateConnectionInfo();
    void showValidationError(const QString &message);
    
    Ui::ConnectionDialog *ui;
    
    // UI组件
    QTabWidget *m_tabWidget;
    
    // 基本连接选项卡
    QWidget *m_basicTab;
    QLineEdit *m_hostEdit;
    QSpinBox *m_portSpinBox;
    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
    QCheckBox *m_savePasswordCheck;
    QCheckBox *m_rememberConnectionCheck;
    
    // 高级选项卡
    QWidget *m_advancedTab;
    QCheckBox *m_fullScreenCheck;
    QComboBox *m_colorDepthCombo;
    QComboBox *m_compressionCombo;
    QCheckBox *m_shareClipboardCheck;
    QCheckBox *m_shareAudioCheck;
    QCheckBox *m_enableEncryptionCheck;
    QSpinBox *m_connectionTimeoutSpinBox;
    

    
    // 按钮
    QPushButton *m_connectButton;
    QPushButton *m_cancelButton;
    QPushButton *m_testButton;
    
    // 状态标签
    QLabel *m_statusLabel;
    
    // 设置
    QSettings *m_settings;
    

    
    // 验证状态
    bool m_isValid;
    QString m_validationError;
    
    // 测试连接相关
    TcpClient *m_testClient;
    QProgressDialog *m_testProgressDialog;
    QTimer *m_testTimer;
};

// 注册ConnectionInfo类型到Qt元类型系统
Q_DECLARE_METATYPE(ConnectionDialog::ConnectionInfo)

#endif // CONNECTIONDIALOG_H