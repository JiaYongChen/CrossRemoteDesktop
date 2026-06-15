#include "ConnectionDialog.h"
#include "ui_ConnectionDialog.h"
#include "../core/config/MessageConstants.h"
#include "../core/config/UiConstants.h"

#include <QtWidgets/QMessageBox>
#include <QtCore/QSettings>
#include <QtGui/QIcon>

ConnectionDialog::ConnectionDialog(QWidget* parent)
	: QDialog(parent)
	, ui(new Ui::ConnectionDialog)
	, m_settings(new QSettings())
	, m_defaultPort(UIConstants::DEFAULT_SERVER_PORT)
{
	ui->setupUi(this);
	ui->togglePasswordBtn->setIcon(QIcon(":/icons/eye.svg"));
	ui->togglePasswordBtn->setIconSize(QSize(14, 14));
	setupConnections();
	loadSettings();
	retranslateButtons();
}

ConnectionDialog::~ConnectionDialog()
{
	delete ui;
}

void ConnectionDialog::setupConnections()
{
	connect(ui->togglePasswordBtn, &QPushButton::clicked,
	        this, &ConnectionDialog::onTogglePasswordClicked);
	connect(ui->fullScreenCheckBox, &QCheckBox::toggled,
	        this, &ConnectionDialog::onFullScreenToggled);
}

void ConnectionDialog::loadSettings()
{
	if (!m_settings) return;
	restoreGeometry(m_settings->value("ConnectionDialog/geometry").toByteArray());

	const QString lastHost = m_settings->value("Connection/lastHost").toString();
	if (!lastHost.isEmpty()) {
		ui->hostLineEdit->setText(lastHost);
	}
	const QString lastUsername = m_settings->value("Connection/lastUsername").toString();
	if (!lastUsername.isEmpty()) {
		ui->usernameLineEdit->setText(lastUsername);
	}
}

void ConnectionDialog::saveSettings()
{
	if (!m_settings) return;
	m_settings->setValue("ConnectionDialog/geometry", saveGeometry());
	m_settings->setValue("Connection/lastHost", ui->hostLineEdit->text().trimmed());
	if (!ui->usernameLineEdit->text().isEmpty()) {
		m_settings->setValue("Connection/lastUsername", ui->usernameLineEdit->text().trimmed());
	}
}

void ConnectionDialog::onTogglePasswordClicked()
{
	const bool isMasked = (ui->passwordLineEdit->echoMode() == QLineEdit::Password);
	ui->passwordLineEdit->setEchoMode(isMasked ? QLineEdit::Normal : QLineEdit::Password);
}

void ConnectionDialog::onFullScreenToggled(bool checked)
{
	ui->windowWidthSpinBox->setEnabled(!checked);
	ui->windowHeightSpinBox->setEnabled(!checked);
	ui->multiplyLabel->setEnabled(!checked);
	ui->windowSizeGroup->setEnabled(!checked);
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
		errorMessage = MessageConstants::UI::INVALID_HOST_ADDRESS;
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
		errorMessage = MessageConstants::UI::INVALID_PORT_RANGE;
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
		QMessageBox::warning(this, MessageConstants::UI::VALIDATION_ERROR_TITLE, errorMessage);
		return;
	}
	saveSettings();
	QDialog::accept();
}

void ConnectionDialog::setDefaultPort(int port)
{
	m_defaultPort = port;
}

void ConnectionDialog::setUsername(const QString& username)
{
	ui->usernameLineEdit->setText(username);
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
