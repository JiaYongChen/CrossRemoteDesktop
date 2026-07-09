#include "ConnectionDialog.h"
#include "ui_ConnectionDialog.h"
#include "../core/config/NetworkConstants.h"
#include "common/core/theme/IconThemeProvider.h"
#include "common/core/theme/TitleBarTheme.h"

#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtGui/QIcon>

ConnectionDialog::ConnectionDialog(QWidget* parent)
	: QDialog(parent)
	, ui(new Ui::ConnectionDialog)
	, m_defaultPort(NetworkConstants::DefaultServerPort)
{
	ui->setupUi(this);
	m_togglePasswordAction = ui->passwordLineEdit->addAction(
		IconThemeProvider::icon("eye-off"), QLineEdit::TrailingPosition);
	connect(m_togglePasswordAction, &QAction::triggered,
	        this, &ConnectionDialog::onTogglePasswordClicked);
	setupConnections();
	retranslateButtons();
}

ConnectionDialog::~ConnectionDialog()
{
	delete ui;
}

void ConnectionDialog::setupConnections()
{
	connect(ui->fullScreenCheckBox, &QCheckBox::toggled,
	        this, &ConnectionDialog::onFullScreenToggled);
}

void ConnectionDialog::onTogglePasswordClicked()
{
	const bool isMasked = (ui->passwordLineEdit->echoMode() == QLineEdit::Password);
	ui->passwordLineEdit->setEchoMode(isMasked ? QLineEdit::Normal : QLineEdit::Password);
	m_togglePasswordAction->setIcon(IconThemeProvider::icon(isMasked
		? "eye"
		: "eye-off"));
}

void ConnectionDialog::refreshIcons()
{
	const bool isMasked = (ui->passwordLineEdit->echoMode() == QLineEdit::Password);
	m_togglePasswordAction->setIcon(IconThemeProvider::icon(isMasked ? "eye" : "eye-off"));
}

void ConnectionDialog::onFullScreenToggled(bool checked)
{
	ui->windowWidthSpinBox->setEnabled(!checked);
	ui->windowHeightSpinBox->setEnabled(!checked);
	ui->multiplyLabel->setEnabled(!checked);
}

void ConnectionDialog::retranslateButtons()
{
	if (QPushButton* okBtn = ui->buttonBox->button(QDialogButtonBox::Ok)) {
		okBtn->setText(tr("连接"));
	}
}

bool ConnectionDialog::validateConnectionInfo(QString& errorMessage) const
{
	const QString input = ui->hostLineEdit->text().trimmed();
	if (input.isEmpty()) {
		errorMessage = tr("请输入有效的主机地址");
		return false;
	}
	if (input.contains(' ')) {
		errorMessage = tr("主机地址不能包含空格");
		return false;
	}
	QString host;
	int port;
	parseHostPort(input, m_defaultPort, host, port);
	if (port < 1 || port > 65535) {
		errorMessage = tr("端口号必须在1-65535之间");
		return false;
	}
	return true;
}

void ConnectionDialog::parseHostPort(const QString& input, int defaultPort,
                                     QString& host, int& port)
{
	if (input.startsWith('[')) {
		const int closeBracket = input.indexOf(']');
		if (closeBracket > 0) {
			host = input.mid(1, closeBracket - 1);
			if (input.size() > closeBracket + 1 && input.at(closeBracket + 1) == ':') {
				port = input.mid(closeBracket + 2).toInt();
			} else {
				port = defaultPort;
			}
			return;
		}
	}
	const int colonIndex = input.lastIndexOf(':');
	if (colonIndex > 0) {
		const QString portStr = input.mid(colonIndex + 1);
		bool isNumber = false;
		port = portStr.toInt(&isNumber);
		if (isNumber) {
			host = input.left(colonIndex);
			return;
		}
	}
	host = input;
	port = defaultPort;
}

void ConnectionDialog::accept()
{
	QString errorMessage;
	if (!validateConnectionInfo(errorMessage)) {
		QMessageBox::warning(this, tr("验证错误"), errorMessage);
		return;
	}
	QDialog::accept();
}

void ConnectionDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	// 每次显示对话框时重新应用标题栏主题
	//（exec() 返回后 Windows 可能重置 DWM 属性）
	TitleBarTheme::apply(this, IconThemeProvider::isDarkMode());
}

void ConnectionDialog::setDefaultPort(int port)
{
	m_defaultPort = port;
}

void ConnectionDialog::setUsername(const QString& username)
{
	ui->usernameLineEdit->setText(username);
}

void ConnectionDialog::setHostname(const QString& hostname)
{
	ui->hostnameLineEdit->setText(hostname);
}

void ConnectionDialog::setHostAddress(const QString& host)
{
	ui->hostLineEdit->setText(host);
}

void ConnectionDialog::setPort(int port)
{
	m_defaultPort = port;
}

void ConnectionDialog::setConnectionParams(const ConnectionParams& params)
{
	ui->hostnameLineEdit->setText(params.hostname);
	ui->hostLineEdit->setText(
		QStringLiteral("%1:%2").arg(params.host).arg(params.port));
	ui->usernameLineEdit->setText(params.username);
	ui->passwordLineEdit->setText(params.password);
	ui->fullScreenCheckBox->setChecked(params.fullScreen);
	ui->windowWidthSpinBox->setValue(params.windowWidth);
	ui->windowHeightSpinBox->setValue(params.windowHeight);
	ui->colorDepthComboBox->setCurrentIndex(
		params.colorDepth == 16 ? 0 : (params.colorDepth == 24 ? 1 : 2));
	ui->qualitySlider->setValue(params.imageQuality);
	ui->viewOnlyCheckBox->setChecked(params.viewOnly);
	ui->clipboardCheckBox->setChecked(params.shareClipboard);
	ui->cursorCheckBox->setChecked(params.showCursor);
	ui->timeoutSpinBox->setValue(params.connectionTimeout / 1000);
	ui->autoReconnectCheckBox->setChecked(params.autoReconnect);
	ui->reconnectIntervalSpinBox->setValue(params.reconnectInterval);
}

ConnectionParams ConnectionDialog::getConnectionParams() const
{
	ConnectionParams p;
	p.host     = getHostAddress();
	p.hostname = getHostname();
	p.port     = getPort();
	p.username = getUsername();
	p.password = getPassword();
	p.colorDepth     = getColorDepth();
	p.fullScreen     = getFullScreen();
	p.windowWidth    = getWindowWidth();
	p.windowHeight   = getWindowHeight();
	p.imageQuality   = getImageQuality();
	p.viewOnly       = getViewOnly();
	p.shareClipboard = getShareClipboard();
	p.showCursor     = getShowCursor();
	p.connectionTimeout  = getConnectionTimeout();
	p.autoReconnect      = getAutoReconnect();
	p.reconnectInterval  = getReconnectInterval();
	return p;
}

void ConnectionDialog::setEditingMode(bool editing)
{
	m_isEditingMode = editing;
	m_togglePasswordAction->setVisible(!editing);
	// 编辑模式下强制密码遮盖，防止通过其他途径切换为明文
	if (editing && ui->passwordLineEdit->echoMode() == QLineEdit::Normal) {
		ui->passwordLineEdit->setEchoMode(QLineEdit::Password);
		m_togglePasswordAction->setIcon(IconThemeProvider::icon("eye-off"));
	}
}

QString ConnectionDialog::getHostname() const
{
	return ui->hostnameLineEdit->text().trimmed();
}

QString ConnectionDialog::getHostAddress() const
{
	QString host;
	int port;
	parseHostPort(ui->hostLineEdit->text().trimmed(), m_defaultPort, host, port);
	return host;
}

int ConnectionDialog::getPort() const
{
	QString host;
	int port;
	parseHostPort(ui->hostLineEdit->text().trimmed(), m_defaultPort, host, port);
	return port;
}

QString ConnectionDialog::getUsername() const
{
	return ui->usernameLineEdit->text().trimmed();
}

QString ConnectionDialog::getPassword() const
{
	return ui->passwordLineEdit->text();
}

int ConnectionDialog::getColorDepth() const
{
	switch (ui->colorDepthComboBox->currentIndex()) {
		case 0: return 16;
		case 1: return 24;
		case 2: return 32;
		default: return 32;
	}
}

bool ConnectionDialog::getFullScreen() const
{
	return ui->fullScreenCheckBox->isChecked();
}

int ConnectionDialog::getWindowWidth() const
{
	return ui->windowWidthSpinBox->value();
}

int ConnectionDialog::getWindowHeight() const
{
	return ui->windowHeightSpinBox->value();
}

int ConnectionDialog::getImageQuality() const
{
	return ui->qualitySlider->value();
}

bool ConnectionDialog::getViewOnly() const
{
	return ui->viewOnlyCheckBox->isChecked();
}

bool ConnectionDialog::getShareClipboard() const
{
	return ui->clipboardCheckBox->isChecked();
}

bool ConnectionDialog::getShowCursor() const
{
	return ui->cursorCheckBox->isChecked();
}

int ConnectionDialog::getConnectionTimeout() const
{
	return ui->timeoutSpinBox->value();
}

bool ConnectionDialog::getAutoReconnect() const
{
	return ui->autoReconnectCheckBox->isChecked();
}

int ConnectionDialog::getReconnectInterval() const
{
	return ui->reconnectIntervalSpinBox->value();
}
