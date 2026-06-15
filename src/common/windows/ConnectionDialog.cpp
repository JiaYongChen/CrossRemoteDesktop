#include "ConnectionDialog.h"
#include "ui_ConnectionDialog.h"
#include "../core/config/UiConstants.h"
#include "../core/config/MessageConstants.h"
#include <algorithm>
#include <QtWidgets/QMessageBox>
#include <QtCore/QSettings>
#include <QtCore/QRegularExpression>
#include <QtGui/QRegularExpressionValidator>

ConnectionDialog::ConnectionDialog(QWidget* parent)
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
	, m_shareClipboardCheck(nullptr)
	, m_enableEncryptionCheck(nullptr)
	, m_connectionTimeoutSpinBox(nullptr)
	, m_connectButton(nullptr)
	, m_cancelButton(nullptr)
	, m_statusLabel(nullptr)
	, m_settings(new QSettings(this))
	, m_isValid(false) {
	ui->setupUi(this);
	setupUI();
	setupConnections();
	setupValidation();
	loadSettings();
}

ConnectionDialog::~ConnectionDialog() {
	delete ui;
}

void ConnectionDialog::setupUI() {
	// 基本UI设置
	setWindowTitle(tr("远程桌面连接"));
	setModal(true);
	resize(UIConstants::CONNECTION_DIALOG_WIDTH, UIConstants::CONNECTION_DIALOG_HEIGHT);
}

void ConnectionDialog::setupConnections() {
	// 连接信号和槽
	connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &ConnectionDialog::accept);
	connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &ConnectionDialog::reject);

	// 密码显示/隐藏切换
	if (ui->togglePasswordBtn) {
		connect(ui->togglePasswordBtn, &QPushButton::clicked, this, [this]() {
			bool isPassword = ui->passwordLineEdit->echoMode() == QLineEdit::Password;
			ui->passwordLineEdit->setEchoMode(isPassword ? QLineEdit::Normal : QLineEdit::Password);
			ui->togglePasswordBtn->setText(isPassword ? QStringLiteral("\U0001F648") : QStringLiteral("\U0001F441"));
		});
	}

	// 全屏模式联动禁用窗口大小控件
	if (ui->fullScreenCheckBox) {
		connect(ui->fullScreenCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
			ui->windowWidthSpinBox->setEnabled(!checked);
			ui->windowHeightSpinBox->setEnabled(!checked);
			ui->multiplyLabel->setEnabled(!checked);
		});
	}
}

void ConnectionDialog::setupValidation() {
	// 设置输入验证 - 主机地址验证
	if (ui && ui->hostLineEdit) {
		connect(ui->hostLineEdit, &QLineEdit::textChanged, this, &ConnectionDialog::onHostChanged);
	}
}

void ConnectionDialog::loadSettings() {
	// 加载设置
	if (!m_settings) return;

	// 加载窗口几何信息
	restoreGeometry(m_settings->value("ConnectionDialog/geometry").toByteArray());

	// 加载最后使用的连接信息（host 可能包含端口，新旧格式兼容）
	QString lastHost = m_settings->value("Connection/lastHost").toString();
	int lastPort = m_settings->value("Connection/lastPort", 5921).toInt();

	// 如果 lastHost 已包含端口（新格式），则直接用；否则拼上端口
	if (!lastHost.isEmpty()) {
		int colonIdx = lastHost.lastIndexOf(':');
		bool hasPort = false;
		if (colonIdx > 0) {
			// 处理 IPv6 [::1]:5921 格式
			if (lastHost.startsWith('[')) {
				int closeBracket = lastHost.lastIndexOf(']');
				hasPort = (closeBracket > 0 && closeBracket < lastHost.length() - 1
				           && lastHost[closeBracket + 1] == ':');
			} else {
				hasPort = true;
			}
		}
		if (hasPort) {
			setHost(lastHost);
		} else {
			setHost(lastHost);
			setPort(lastPort);
		}
	}
	setUsername(m_settings->value("Connection/lastUsername").toString());

	// 加载连接选项
	if (ui) {
		if (ui->fullScreenCheckBox) {
			ui->fullScreenCheckBox->setChecked(
			    m_settings->value("Connection/fullScreen", false).toBool());
		}
		if (ui->colorDepthComboBox) {
			int colorDepth = m_settings->value("Connection/colorDepth", 32).toInt();
			int index = 2; // 默认32位
			if (colorDepth == 16) index = 0;
			else if (colorDepth == 24) index = 1;
			ui->colorDepthComboBox->setCurrentIndex(index);
		}
		if (ui->clipboardCheckBox) {
			ui->clipboardCheckBox->setChecked(
			    m_settings->value("Connection/shareClipboard", true).toBool());
		}
		if (ui->autoReconnectCheckBox) {
			ui->autoReconnectCheckBox->setChecked(
			    m_settings->value("Connection/autoReconnect", false).toBool());
		}
		if (ui->timeoutSpinBox) {
			ui->timeoutSpinBox->setValue(
			    m_settings->value("Connection/timeout", 30).toInt());
		}
		if (ui->reconnectIntervalSpinBox) {
			ui->reconnectIntervalSpinBox->setValue(
			    m_settings->value("Connection/reconnectInterval", 5).toInt());
		}
		if (ui->qualitySlider) {
			ui->qualitySlider->setValue(
			    m_settings->value("Connection/quality", 85).toInt());
		}
		if (ui->cursorCheckBox) {
			ui->cursorCheckBox->setChecked(
			    m_settings->value("Connection/showCursor", true).toBool());
		}
		if (ui->viewOnlyCheckBox) {
			ui->viewOnlyCheckBox->setChecked(
			    m_settings->value("Connection/viewOnly", false).toBool());
		}
		if (ui->hostnameLineEdit) {
			ui->hostnameLineEdit->setText(
			    m_settings->value("Connection/hostname").toString());
		}
	}
}

void ConnectionDialog::saveSettings() {
	// 保存设置
	if (!m_settings) return;

	// 保存窗口几何信息
	m_settings->setValue("ConnectionDialog/geometry", saveGeometry());

	// 保存当前连接信息（host 已包含端口）
	m_settings->setValue("Connection/lastHost", getHost());
	m_settings->setValue("Connection/lastPort", getPort());
	m_settings->setValue("Connection/lastUsername", getUsername());

	// 保存连接选项
	m_settings->setValue("Connection/fullScreen", getFullScreen());
	m_settings->setValue("Connection/colorDepth", getColorDepth());
	m_settings->setValue("Connection/shareClipboard", getShareClipboard());
	if (ui) {
		if (ui->autoReconnectCheckBox)
			m_settings->setValue("Connection/autoReconnect",
			                     ui->autoReconnectCheckBox->isChecked());
		if (ui->timeoutSpinBox)
			m_settings->setValue("Connection/timeout", ui->timeoutSpinBox->value());
		if (ui->reconnectIntervalSpinBox)
			m_settings->setValue("Connection/reconnectInterval",
			                     ui->reconnectIntervalSpinBox->value());
		if (ui->qualitySlider)
			m_settings->setValue("Connection/quality", ui->qualitySlider->value());
		if (ui->cursorCheckBox)
			m_settings->setValue("Connection/showCursor",
			                     ui->cursorCheckBox->isChecked());
		if (ui->viewOnlyCheckBox)
			m_settings->setValue("Connection/viewOnly",
			                     ui->viewOnlyCheckBox->isChecked());
		if (ui->hostnameLineEdit)
			m_settings->setValue("Connection/hostname",
			                     ui->hostnameLineEdit->text());
	}
}

bool ConnectionDialog::validateConnectionInfo() {
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

void ConnectionDialog::showValidationError(const QString& message) {
	// 显示验证错误
	QMessageBox::warning(this, MessageConstants::UI::VALIDATION_ERROR_TITLE, message);
}

// 连接信息获取
QString ConnectionDialog::getHost() const {
	if (ui && ui->hostLineEdit) {
		return ui->hostLineEdit->text().trimmed();
	}
	return QString();
}

int ConnectionDialog::getPort() const {
	if (ui && ui->hostLineEdit) {
		QString host = ui->hostLineEdit->text().trimmed();
		// 处理 IPv6 地址格式 [::1]:5921
		if (host.startsWith('[')) {
			int closeBracket = host.lastIndexOf(']');
			if (closeBracket > 0 && closeBracket < host.length() - 1
			    && host[closeBracket + 1] == ':') {
				bool ok;
				int port = host.mid(closeBracket + 2).toInt(&ok);
				if (ok && port > 0 && port <= 65535) return port;
			}
		} else {
			int colonIdx = host.lastIndexOf(':');
			if (colonIdx > 0) {
				bool ok;
				int port = host.mid(colonIdx + 1).toInt(&ok);
				if (ok && port > 0 && port <= 65535) return port;
			}
		}
	}
	return 5921; // 默认端口
}

QString ConnectionDialog::getUsername() const {
	if (ui && ui->usernameLineEdit) {
		return ui->usernameLineEdit->text().trimmed();
	}
	return QString();
}

QString ConnectionDialog::getPassword() const {
	if (ui && ui->passwordLineEdit) {
		return ui->passwordLineEdit->text();
	}
	return QString();
}

// 连接选项
bool ConnectionDialog::getFullScreen() const {
	if (ui && ui->fullScreenCheckBox) {
		return ui->fullScreenCheckBox->isChecked();
	}
	return false;
}

int ConnectionDialog::getColorDepth() const {
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

bool ConnectionDialog::getShareClipboard() const {
	if (ui && ui->clipboardCheckBox) {
		return ui->clipboardCheckBox->isChecked();
	}
	return true;
}

// 设置连接信息
void ConnectionDialog::setHost(const QString& host) {
	if (ui && ui->hostLineEdit) {
		// 保留当前已有端口（如果 host 参数不含端口）
		QString current = ui->hostLineEdit->text().trimmed();
		int colonIdx = current.lastIndexOf(':');
		bool currentHasPort = false;
		QString currentPort;
		if (colonIdx > 0) {
			if (current.startsWith('[')) {
				int closeBracket = current.lastIndexOf(']');
				if (closeBracket > 0 && closeBracket < current.length() - 1
				    && current[closeBracket + 1] == ':') {
					currentHasPort = true;
					currentPort = current.mid(closeBracket + 1);
				}
			} else {
				currentHasPort = true;
				currentPort = current.mid(colonIdx);
			}
		}

		// 检查新 host 是否已含端口
		QString newHost = host.trimmed();
		int newColonIdx = newHost.lastIndexOf(':');
		bool newHasPort = false;
		if (newColonIdx > 0) {
			if (newHost.startsWith('[')) {
				int closeBracket = newHost.lastIndexOf(']');
				newHasPort = (closeBracket > 0 && closeBracket < newHost.length() - 1
				              && newHost[closeBracket + 1] == ':');
			} else {
				newHasPort = true;
			}
		}

		if (newHasPort) {
			ui->hostLineEdit->setText(newHost);
		} else if (currentHasPort) {
			ui->hostLineEdit->setText(newHost + currentPort);
		} else {
			ui->hostLineEdit->setText(newHost);
		}
	}
}

void ConnectionDialog::setPort(int port) {
	if (ui && ui->hostLineEdit) {
		QString current = ui->hostLineEdit->text().trimmed();
		// 剥离已有端口
		if (current.startsWith('[')) {
			int closeBracket = current.lastIndexOf(']');
			if (closeBracket > 0) {
				current = current.left(closeBracket + 1);
			}
		} else {
			int colonIdx = current.lastIndexOf(':');
			if (colonIdx > 0) {
				// 确认冒号后是数字（非 IPv6 冒号）
				bool ok;
				current.mid(colonIdx + 1).toInt(&ok);
				if (ok) {
					current = current.left(colonIdx);
				}
			}
		}
		ui->hostLineEdit->setText(QString("%1:%2").arg(current).arg(port));
	}
}

void ConnectionDialog::setUsername(const QString& username) {
	if (ui && ui->usernameLineEdit) {
		ui->usernameLineEdit->setText(username);
	}
}

void ConnectionDialog::setPassword(const QString& password) {
	if (ui && ui->passwordLineEdit) {
		ui->passwordLineEdit->setText(password);
	}
}

// 槽函数
void ConnectionDialog::onConnectClicked() {
	accept();
}

void ConnectionDialog::onCancelClicked() {
	reject();
}

void ConnectionDialog::onAdvancedToggled(bool show) {
	Q_UNUSED(show)
}

void ConnectionDialog::onHostChanged() {
	validateInput();
}

void ConnectionDialog::onPortChanged() {
	validateInput();
}

void ConnectionDialog::validateInput() {
	// 验证输入
	m_isValid = validateConnectionInfo();
}

void ConnectionDialog::accept() {
	// 验证输入
	if (!validateConnectionInfo()) {
		QMessageBox::warning(this, MessageConstants::UI::INPUT_ERROR_TITLE,
		                     m_validationError);
		return;
	}

	// 构建连接信息
	ConnectionInfo info;
	QString host = getHost();
	int port = getPort();
	info.host = host;
	info.port = port;
	info.username = getUsername();

	// 使用主机名或默认格式作为连接名称
	if (ui && ui->hostnameLineEdit && !ui->hostnameLineEdit->text().isEmpty()) {
		info.name = ui->hostnameLineEdit->text().trimmed();
	} else {
		info.name = QString("%1:%2").arg(host).arg(port);
		if (!info.username.isEmpty()) {
			info.name = QString("%1@%2:%3").arg(info.username).arg(host).arg(port);
		}
	}
	info.lastUsed = QDateTime::currentDateTime();

	// 保存设置
	saveSettings();

	// 接受对话框
	QDialog::accept();
}

void ConnectionDialog::reject() {
	QDialog::reject();
}
