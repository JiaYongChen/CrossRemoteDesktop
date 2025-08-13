#include "connectiondialog.h"
#include "ui_connectiondialog.h"
#include "../../client/tcpclient.h"
#include "../core/uiconstants.h"
#include "../core/messageconstants.h"
#include <algorithm>
#include <QtWidgets/QMessageBox>
#include <QSettings>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QIntValidator>
#include <QTimer>
#include <QProgressDialog>
#include <algorithm>

ConnectionDialog::ConnectionDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ConnectionDialog)
    , m_tabWidget(nullptr)
    , m_basicTab(nullptr)
    , m_hostEdit(nullptr)
    , m_portSpinBox(nullptr)
    , m_usernameEdit(nullptr)
    , m_passwordEdit(nullptr)
    , m_savePasswordCheck(nullptr)
    , m_rememberConnectionCheck(nullptr)
    , m_advancedTab(nullptr)
    , m_fullScreenCheck(nullptr)
    , m_colorDepthCombo(nullptr)
    , m_compressionCombo(nullptr)
    , m_shareClipboardCheck(nullptr)
    , m_shareAudioCheck(nullptr)
    , m_enableEncryptionCheck(nullptr)
    , m_connectionTimeoutSpinBox(nullptr)
    , m_connectButton(nullptr)
    , m_cancelButton(nullptr)
    , m_testButton(nullptr)
    , m_statusLabel(nullptr)
    , m_settings(new QSettings(this))
    , m_isValid(false)
    , m_testClient(nullptr)
    , m_testProgressDialog(nullptr)
    , m_testTimer(nullptr)
{
    ui->setupUi(this);
    setupUI();
    setupConnections();
    setupValidation();
    loadSettings();
}

ConnectionDialog::~ConnectionDialog()
{
    delete ui;
}

void ConnectionDialog::setupUI()
{
    // 基本UI设置
    setWindowTitle(tr("远程桌面连接"));
    setModal(true);
    resize(UIConstants::CONNECTION_DIALOG_WIDTH, UIConstants::CONNECTION_DIALOG_HEIGHT);
}

void ConnectionDialog::setupConnections()
{
    // 连接信号槽
    connect(ui->testConnectionButton, &QPushButton::clicked, this, &ConnectionDialog::onTestConnectionClicked);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &ConnectionDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &ConnectionDialog::reject);
    

}

void ConnectionDialog::setupValidation()
{
    // 设置输入验证
    if (ui) {
        // 主机地址验证 - 允许IP地址和主机名
        if (ui->hostLineEdit) {
            connect(ui->hostLineEdit, &QLineEdit::textChanged, this, &ConnectionDialog::onHostChanged);
        }
        
        // 端口验证
        if (ui->portSpinBox) {
            connect(ui->portSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConnectionDialog::onPortChanged);
        }
    }
}

void ConnectionDialog::loadSettings()
{
    // 加载设置
    if (!m_settings) return;
    
    // 加载窗口几何信息
    restoreGeometry(m_settings->value("ConnectionDialog/geometry").toByteArray());
    
    // 加载最后使用的连接信息
    setHost(m_settings->value("Connection/lastHost").toString());
    setPort(m_settings->value("Connection/lastPort", 5900).toInt());
    setUsername(m_settings->value("Connection/lastUsername").toString());
    
    // 加载连接选项
    if (ui) {
        if (ui->fullScreenCheckBox) {
            ui->fullScreenCheckBox->setChecked(m_settings->value("Connection/fullScreen", false).toBool());
        }
        if (ui->colorDepthComboBox) {
            int colorDepth = m_settings->value("Connection/colorDepth", 32).toInt();
            int index = 2; // 默认32位
            if (colorDepth == 16) index = 0;
            else if (colorDepth == 24) index = 1;
            ui->colorDepthComboBox->setCurrentIndex(index);
        }
        if (ui->compressionSlider) {
            ui->compressionSlider->setValue(m_settings->value("Connection/compression", 6).toInt());
        }
        if (ui->clipboardCheckBox) {
            ui->clipboardCheckBox->setChecked(m_settings->value("Connection/shareClipboard", true).toBool());
        }
        if (ui->audioCheckBox) {
            ui->audioCheckBox->setChecked(m_settings->value("Connection/shareAudio", false).toBool());
        }
        if (ui->savePasswordCheckBox) {
            ui->savePasswordCheckBox->setChecked(m_settings->value("Connection/savePassword", false).toBool());
        }
    }
    

}

void ConnectionDialog::saveSettings()
{
    // 保存设置
    if (!m_settings) return;
    
    // 保存窗口几何信息
    m_settings->setValue("ConnectionDialog/geometry", saveGeometry());
    
    // 保存当前连接信息
    m_settings->setValue("Connection/lastHost", getHost());
    m_settings->setValue("Connection/lastPort", getPort());
    m_settings->setValue("Connection/lastUsername", getUsername());
    
    // 保存连接选项
    m_settings->setValue("Connection/fullScreen", getFullScreen());
    m_settings->setValue("Connection/colorDepth", getColorDepth());
    m_settings->setValue("Connection/compression", getCompressionLevel());
    m_settings->setValue("Connection/shareClipboard", getShareClipboard());
    m_settings->setValue("Connection/shareAudio", getShareAudio());
    
    if (ui && ui->savePasswordCheckBox && ui->savePasswordCheckBox->isChecked()) {
        m_settings->setValue("Connection/savePassword", true);
        // 注意：实际应用中应该加密保存密码
        m_settings->setValue("Connection/lastPassword", getPassword());
    } else {
        m_settings->setValue("Connection/savePassword", false);
        m_settings->remove("Connection/lastPassword");
    }
    

}

void ConnectionDialog::createBasicTab()
{
    // 创建基本选项卡
}

void ConnectionDialog::createAdvancedTab()
{
    // 创建高级选项卡
}

bool ConnectionDialog::validateConnectionInfo()
{
    // 验证连接信息
    m_validationError.clear();
    
    // 验证主机地址
    QString host = getHost();
    if (host.isEmpty()) {
        m_validationError = MessageConstants::UI::INVALID_HOST_ADDRESS;
        return false;
    }
    
    // 验证主机地址格式（简单验证）
    if (host.contains(" ") || host.contains("\t")) {
        m_validationError = tr("主机地址不能包含空格");
        return false;
    }
    
    // 验证端口
    int port = getPort();
    if (port < 1 || port > 65535) {
        m_validationError = MessageConstants::UI::INVALID_PORT_RANGE;
        return false;
    }
    
    return true;
}

void ConnectionDialog::showValidationError(const QString &message)
{
    // 显示验证错误
    QMessageBox::warning(this, MessageConstants::UI::VALIDATION_ERROR_TITLE, message);
}

// 连接信息获取
QString ConnectionDialog::getHost() const
{
    if (ui && ui->hostLineEdit) {
        return ui->hostLineEdit->text().trimmed();
    }
    return QString();
}

int ConnectionDialog::getPort() const
{
    if (ui && ui->portSpinBox) {
        return ui->portSpinBox->value();
    }
    return 5900; // 默认端口
}

QString ConnectionDialog::getUsername() const
{
    if (ui && ui->usernameLineEdit) {
        return ui->usernameLineEdit->text().trimmed();
    }
    return QString();
}

QString ConnectionDialog::getPassword() const
{
    if (ui && ui->passwordLineEdit) {
        return ui->passwordLineEdit->text();
    }
    return QString();
}

// 连接选项
bool ConnectionDialog::getFullScreen() const
{
    if (ui && ui->fullScreenCheckBox) {
        return ui->fullScreenCheckBox->isChecked();
    }
    return false;
}

int ConnectionDialog::getColorDepth() const
{
    if (ui && ui->colorDepthComboBox) {
        int index = ui->colorDepthComboBox->currentIndex();
        switch (index) {
            case 0: return 16;  // 16位
            case 1: return 24;  // 24位
            case 2: return 32;  // 32位
            default: return 32;
        }
    }
    return 32;
}

int ConnectionDialog::getCompressionLevel() const
{
    if (ui && ui->compressionSlider) {
        return ui->compressionSlider->value();
    }
    return 6; // 默认压缩级别
}

bool ConnectionDialog::getShareClipboard() const
{
    if (ui && ui->clipboardCheckBox) {
        return ui->clipboardCheckBox->isChecked();
    }
    return true;
}

bool ConnectionDialog::getShareAudio() const
{
    if (ui && ui->audioCheckBox) {
        return ui->audioCheckBox->isChecked();
    }
    return false;
}

// 设置连接信息
void ConnectionDialog::setHost(const QString &host)
{
    if (ui && ui->hostLineEdit) {
        ui->hostLineEdit->setText(host);
    }
}

void ConnectionDialog::setPort(int port)
{
    if (ui && ui->portSpinBox) {
        ui->portSpinBox->setValue(port);
    }
}

void ConnectionDialog::setUsername(const QString &username)
{
    if (ui && ui->usernameLineEdit) {
        ui->usernameLineEdit->setText(username);
    }
}

void ConnectionDialog::setPassword(const QString &password)
{
    if (ui && ui->passwordLineEdit) {
        ui->passwordLineEdit->setText(password);
    }
}

// 槽函数
void ConnectionDialog::onConnectClicked()
{
    accept();
}

void ConnectionDialog::onCancelClicked()
{
    reject();
}

void ConnectionDialog::onTestConnectionClicked()
{
    // 测试连接
    if (!validateConnectionInfo()) {
        QMessageBox::warning(this, MessageConstants::UI::INPUT_ERROR_TITLE, m_validationError);
        return;
    }
    
    QString host = getHost();
    int port = getPort();
    
    if (host.isEmpty()) {
        QMessageBox::warning(this, MessageConstants::UI::INPUT_ERROR_TITLE, MessageConstants::UI::INVALID_HOST_ADDRESS);
        return;
    }
    
    // 创建测试客户端
    if (!m_testClient) {
        m_testClient = new TcpClient(this);
        connect(m_testClient, &TcpClient::connected, this, &ConnectionDialog::onTestConnected);
        connect(m_testClient, &TcpClient::disconnected, this, &ConnectionDialog::onTestDisconnected);
        connect(m_testClient, &TcpClient::errorOccurred, this, &ConnectionDialog::onTestError);
    }
    
    // 创建进度对话框
    if (!m_testProgressDialog) {
        m_testProgressDialog = new QProgressDialog(this);
        m_testProgressDialog->setWindowTitle(tr("测试连接"));
        m_testProgressDialog->setLabelText(tr("正在连接到 %1:%2...").arg(host).arg(port));
        m_testProgressDialog->setRange(0, 0); // 无限进度条
        m_testProgressDialog->setModal(true);
        connect(m_testProgressDialog, &QProgressDialog::canceled, this, [this]() {
            if (m_testClient) {
                m_testClient->disconnectFromHost();
            }
        });
    }
    
    // 创建超时定时器
    if (!m_testTimer) {
        m_testTimer = new QTimer(this);
        m_testTimer->setSingleShot(true);
        connect(m_testTimer, &QTimer::timeout, this, &ConnectionDialog::onTestTimeout);
    }
    
    // 开始连接测试
    m_testProgressDialog->show();
    m_testTimer->start(10000); // 10秒超时
    m_testClient->connectToHost(host, port);
}

void ConnectionDialog::onAdvancedToggled(bool show)
{
    Q_UNUSED(show)
}

void ConnectionDialog::onHostChanged()
{
    validateInput();
}

void ConnectionDialog::onPortChanged()
{
    validateInput();
}

void ConnectionDialog::validateInput()
{
    // 验证输入
    m_isValid = validateConnectionInfo();
    
    // 更新UI状态（如果需要）
    if (ui && ui->testConnectionButton) {
        ui->testConnectionButton->setEnabled(m_isValid && !getHost().isEmpty());
    }
}

void ConnectionDialog::accept()
{
    // 验证输入
    if (!validateConnectionInfo()) {
        QMessageBox::warning(this, MessageConstants::UI::INPUT_ERROR_TITLE, m_validationError);
        return;
    }
    
    // 保存连接信息到历史记录
    ConnectionInfo info;
    info.name = ui->nameLineEdit ? ui->nameLineEdit->text() : QString("%1:%2").arg(getHost()).arg(getPort());
    info.host = getHost();
    info.port = getPort();
    info.username = getUsername();
    info.lastUsed = QDateTime::currentDateTime();
    
    // 如果连接名称为空，使用默认格式
    if (info.name.isEmpty()) {
        info.name = QString("%1:%2").arg(info.host).arg(info.port);
        if (!info.username.isEmpty()) {
            info.name = QString("%1@%2:%3").arg(info.username).arg(info.host).arg(info.port);
        }
    }
    
    // 历史记录功能已移除
    
    // 保存设置
    saveSettings();
    
    // 接受对话框
    QDialog::accept();
}

void ConnectionDialog::reject()
{
    // 清理测试连接资源
    if (m_testClient) {
        m_testClient->abort();
        m_testClient->deleteLater();
        m_testClient = nullptr;
    }
    if (m_testTimer) {
        m_testTimer->stop();
        m_testTimer->deleteLater();
        m_testTimer = nullptr;
    }
    if (m_testProgressDialog) {
        m_testProgressDialog->close();
        m_testProgressDialog->deleteLater();
        m_testProgressDialog = nullptr;
    }
    
    QDialog::reject();
}

// 测试连接相关槽函数
void ConnectionDialog::onTestConnected()
{
    // 测试连接成功
    if (m_testTimer) {
        m_testTimer->stop();
    }
    
    if (m_testProgressDialog) {
        m_testProgressDialog->hide();
    }
    
    QMessageBox::information(this, tr("连接测试"), tr("连接成功！"));
    
    // 断开测试连接
    if (m_testClient) {
        m_testClient->disconnectFromHost();
    }
}

void ConnectionDialog::onTestDisconnected()
{
    // 测试连接断开
    if (m_testProgressDialog && m_testProgressDialog->isVisible()) {
        m_testProgressDialog->hide();
    }
}

void ConnectionDialog::onTestError(const QString &error)
{
    // 测试连接错误
    if (m_testTimer) {
        m_testTimer->stop();
    }
    
    if (m_testProgressDialog) {
        m_testProgressDialog->hide();
    }
    
    QMessageBox::critical(this, tr("连接测试失败"), 
                         tr("无法连接到服务器：\n%1").arg(error));
}

void ConnectionDialog::onTestTimeout()
{
    // 测试连接超时
    if (m_testClient) {
        m_testClient->disconnectFromHost();
    }
    
    if (m_testProgressDialog) {
        m_testProgressDialog->hide();
    }
    
    QMessageBox::warning(this, tr("连接超时"), 
                        tr("连接超时，请检查网络连接和服务器地址。"));
}