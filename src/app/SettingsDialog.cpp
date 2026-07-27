#include "SettingsDialog.h"
#include "ui_SettingsDialog.h"
#include "common/config/TranslationUtils.h"
#include "common/logging/LoggingCategories.h"
#include "common/config/NetworkConstants.h"
#include "common/crypto/PasswordCrypto.h"
#include "common/theme/IconThemeProvider.h"
#include "common/theme/TitleBarTheme.h"

#include "common/config/SettingsManager.h"
#include "common/platform/AutoStartManager.h"
#include <QtCore/QEvent>
#include <QtGui/QCloseEvent>
#include <QtGui/QHideEvent>
#include <QtCore/QVariant>
#include <QtCore/QByteArray>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QPushButton>
#include <QtGui/QIcon>

SettingsDialog::SettingsDialog(SettingsManager *settings,
                               AutoStartManager *autoStartMgr,
                               QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::SettingsDialog)
	, m_settings(settings)
	, m_autoStartMgr(autoStartMgr)
{
	ui->setupUi(this);
	setWindowIcon(IconThemeProvider::icon("settings"));
	m_togglePasswordAction = ui->passwordEdit->addAction(
		IconThemeProvider::icon("eye-off"), QLineEdit::TrailingPosition);
	connect(m_togglePasswordAction, &QAction::triggered,
	        this, &SettingsDialog::onTogglePasswordClicked);
	setupUI();
	setupConnections();
	loadSettings();
}

SettingsDialog::~SettingsDialog()
{
	// 析构前确保持久化所有未保存的配置变更
	m_settings->save();
	delete ui;
}

void SettingsDialog::setupUI()
{
	connect(ui->categoryListWidget, &QListWidget::currentRowChanged,
			ui->settingsStackedWidget, &QStackedWidget::setCurrentIndex);
	ui->categoryListWidget->setCurrentRow(0);
	updateLanguageList();
}

void SettingsDialog::setupConnections()
{
	// 常规 — 语言
	connect(ui->languageComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
			this, &SettingsDialog::onLanguageChanged);

	// 常规 — 开机自动启动
	connect(ui->autoStartCheckBox, &QCheckBox::toggled,
	        this, &SettingsDialog::onAutoStartChanged);

	// 常规 — 关闭行为
	connect(ui->closeToTrayCheckBox, &QCheckBox::toggled,
	        this, &SettingsDialog::onCloseToTrayChanged);

	// 通信 — 监听端口
	connect(ui->listenPortSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
			this, &SettingsDialog::onListenPortChanged);

	// 通信 — 用户名（editingFinished: 只在用户完成编辑时触发，避免每次按键都触发密码重加密）
	connect(ui->usernameEdit, &QLineEdit::editingFinished,
			this, &SettingsDialog::onUsernameChanged);

	// 通信 — 密码
	connect(ui->passwordEdit, &QLineEdit::editingFinished,
			this, &SettingsDialog::onPasswordChanged);

	// 高级 — 日志级别
	connect(ui->logLevelComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
			this, &SettingsDialog::onLogLevelChanged);

	// 高级 — 日志规则
	connect(ui->logRulesTextEdit, &QTextEdit::textChanged,
			this, &SettingsDialog::onLogRulesChanged);

	// 高级 — 预设/重置按钮
	connect(ui->presetDebugBtn, &QPushButton::clicked,
			this, &SettingsDialog::onPresetDebugClicked);
	connect(ui->resetRulesBtn, &QPushButton::clicked,
			this, &SettingsDialog::onResetRulesClicked);

	// 底部 — 恢复默认值
	connect(ui->restoreDefaultsBtn, &QPushButton::clicked,
			this, &SettingsDialog::onRestoreDefaultsClicked);
}

void SettingsDialog::updateLanguageList()
{
	// 阻止信号避免 setCurrentIndex 触发 onLanguageChanged 造成死循环
	ui->languageComboBox->blockSignals(true);

	ui->languageComboBox->clear();
	ui->languageComboBox->addItem(tr("中文"), QVariant(QStringLiteral("zh_CN")));
	ui->languageComboBox->addItem(tr("English"), QVariant(QStringLiteral("en_US")));

	const QString currentLang = m_settings->getString("General/language", "zh_CN");
	const int idx = ui->languageComboBox->findData(QVariant(currentLang));
	if (idx >= 0) ui->languageComboBox->setCurrentIndex(idx);

	ui->languageComboBox->blockSignals(false);
}

void SettingsDialog::loadSettings()
{
	// 常规 — 开机自启动：以 OS 实际状态为准
	const bool configAutoStart = m_settings->getBool("General/startWithSystem", false);
	const bool osAutoStart = m_autoStartMgr->isAutoStartEnabled();

	if (configAutoStart != osAutoStart) {
		// 配置与 OS 不一致 → 以 OS 为准，修正配置
		m_settings->setBool("General/startWithSystem", osAutoStart);
		if (configAutoStart && !osAutoStart) {
			qCWarning(lcUISettingsDialog) << "开机自启动注册失效（可能应用已移动），已自动禁用";
		}
	}

	// 使用 blockSignals 防止 setChecked 的 toggled 信号触发 onAutoStartChanged
	// 进而产生不必要的 OS API 调用（loadSettings 只是加载状态，不是用户操作）
	ui->autoStartCheckBox->blockSignals(true);
	ui->autoStartCheckBox->setChecked(osAutoStart);
	ui->autoStartCheckBox->blockSignals(false);

	const bool closeToTray = m_settings->getBool("UI/closeToTray", false);
	ui->closeToTrayCheckBox->setChecked(closeToTray);

	// 通信
	const int listenPort = m_settings->getInt("Server/listenPort", NetworkConstants::DefaultServerPort);
	ui->listenPortSpinBox->setValue(listenPort);

	const QString username = m_settings->getString("Server/username");
	ui->usernameEdit->setText(username);

	const QString encryptedPassword = m_settings->getString("Server/password");
	m_cachedPassword = PasswordCrypto::decrypt(username, encryptedPassword);
	ui->passwordEdit->setText(m_cachedPassword);

	// 高级
	const QString logLevel = m_settings->getString("Logging/level", "info").toLower();
	int levelIdx = 2; // default: info
	if (logLevel == "error" || logLevel == QStringLiteral("错误")) levelIdx = 0;
	else if (logLevel == "warning" || logLevel == QStringLiteral("警告")) levelIdx = 1;
	else if (logLevel == "debug" || logLevel == QStringLiteral("调试")) levelIdx = 3;
	ui->logLevelComboBox->setCurrentIndex(levelIdx);

	const QString logRules = m_settings->getString("Logging/rules");
	ui->logRulesTextEdit->setPlainText(logRules);
}

// ===== 即时生效槽 =====

void SettingsDialog::onLanguageChanged(int index)
{
	const QString lang = ui->languageComboBox->itemData(index).toString();
	if (lang.isEmpty() || lang == m_lastLang) return;
	m_lastLang = lang;

	m_settings->setString("General/language", lang);
	switchTranslation(*qApp, lang);
	qCInfo(lcUISettingsDialog) << "SettingsDialog: language switched to" << lang;
}

void SettingsDialog::onAutoStartChanged(bool checked)
{
	// 先调用 OS API 注册/注销自启动，成功后再持久化配置
	if (!m_autoStartMgr->setAutoStart(checked)) {
		qCWarning(lcUISettingsDialog) << "开机自启动注册失败:"
		                              << m_autoStartMgr->lastError();
		// 回滚 UI 状态
		ui->autoStartCheckBox->blockSignals(true);
		ui->autoStartCheckBox->setChecked(!checked);
		ui->autoStartCheckBox->blockSignals(false);
		return;
	}

	m_settings->setBool("General/startWithSystem", checked);

	qCInfo(lcUISettingsDialog) << "开机自启动设置为" << checked;
}

void SettingsDialog::onCloseToTrayChanged(bool checked)
{
	m_settings->setBool("UI/closeToTray", checked);
	qCDebug(lcUISettingsDialog) << "SettingsDialog: close to tray set to" << checked;
}

void SettingsDialog::onListenPortChanged(int value)
{
	m_settings->setInt("Server/listenPort", value);
	qCDebug(lcUISettingsDialog) << "SettingsDialog: listen port set to" << value;
}

void SettingsDialog::onUsernameChanged()
{
	const QString oldUsername = m_settings->getString("Server/username");
	const QString newUsername = ui->usernameEdit->text();

	if (newUsername == oldUsername) return;

	// 仅内存缓存，退出设置界面时由 saveAuthConfig() 统一持久化 + done() 互斥校验
	qCDebug(lcUISettingsDialog) << "SettingsDialog: username changed (pending)";
}

void SettingsDialog::onPasswordChanged()
{
	const QString newPassword = ui->passwordEdit->text();
	if (newPassword == m_cachedPassword) return;

	// 仅内存缓存，退出设置界面时由 saveAuthConfig() 统一持久化 + done() 互斥校验
	m_cachedPassword = newPassword;
	qCDebug(lcUISettingsDialog) << "SettingsDialog: password updated (pending)";
}

void SettingsDialog::onLogLevelChanged(int index)
{
	static const char* levels[] = {"error", "warning", "info", "debug"};
	if (index < 0 || index > 3) return;

	m_settings->setString("Logging/level", levels[index]);
	qCDebug(lcUISettingsDialog) << "SettingsDialog: log level set to" << levels[index];
}

void SettingsDialog::onLogRulesChanged()
{
	const QString rules = ui->logRulesTextEdit->toPlainText();
	m_settings->setString("Logging/rules", rules);
	applyLogRules();
}

void SettingsDialog::applyLogRules()
{
	const QByteArray envRules = qgetenv("QT_LOGGING_RULES");
	if (envRules.isEmpty()) {
		const QString rules = ui->logRulesTextEdit->toPlainText().trimmed();
		if (!rules.isEmpty()) {
			QLoggingCategory::setFilterRules(rules);
		}
	}
}

// ===== 按钮槽 =====

void SettingsDialog::onRestoreDefaultsClicked()
{
	ui->languageComboBox->setCurrentIndex(0);
	onLanguageChanged(0);

	// 自启动：用 blockSignals 阻止 setChecked 触发 onAutoStartChanged
	// （直接设置配置 + 调用 OS API，避免信号竞态和提前 return）
	ui->autoStartCheckBox->blockSignals(true);
	ui->autoStartCheckBox->setChecked(false);
	ui->autoStartCheckBox->blockSignals(false);
	m_settings->setBool("General/startWithSystem", false);
	static_cast<void>(m_autoStartMgr->setAutoStart(false));

	ui->closeToTrayCheckBox->setChecked(false);
	onCloseToTrayChanged(false);

	ui->listenPortSpinBox->setValue(NetworkConstants::DefaultServerPort);
	onListenPortChanged(NetworkConstants::DefaultServerPort);

	ui->usernameEdit->clear();
	ui->passwordEdit->clear();
	m_cachedPassword.clear();
	m_settings->remove("Server/username");
	m_settings->remove("Server/password");

	ui->logLevelComboBox->setCurrentIndex(2);
	onLogLevelChanged(2);

	ui->logRulesTextEdit->clear();
	m_settings->remove("Logging/rules");

	qCDebug(lcUISettingsDialog) << "SettingsDialog: restored to defaults";
}

void SettingsDialog::onTogglePasswordClicked()
{
	const bool isMasked = (ui->passwordEdit->echoMode() == QLineEdit::Password);
	ui->passwordEdit->setEchoMode(isMasked ? QLineEdit::Normal : QLineEdit::Password);
	m_togglePasswordAction->setIcon(IconThemeProvider::icon(isMasked
		? "eye"
		: "eye-off"));
}

void SettingsDialog::onPresetDebugClicked()
{
	const QString coreRules =
		"lcApp.debug=true\n"
		"lcServer.debug=true\n"
		"lcClient.debug=true\n"
		"lcUISettingsDialog.debug=true\n"
		"lcCoreProtocol.debug=true\n"
		"core.*.debug=true\n"
		"qt.network.ssl.warning=false";
	ui->logRulesTextEdit->setPlainText(coreRules);
}

void SettingsDialog::onResetRulesClicked()
{
	ui->logRulesTextEdit->clear();
}

// ===== 语言切换事件 =====

void SettingsDialog::changeEvent(QEvent* event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::LanguageChange) {
		const QString currentLocale = ui->languageComboBox->currentData().toString();

		ui->retranslateUi(this);
		updateLanguageList();

		if (!currentLocale.isEmpty()) {
			// 静默恢复选中项（updateLanguageList 已设置正确索引，此处补充防御）
			const int idx = ui->languageComboBox->findData(currentLocale);
			if (idx >= 0) {
				ui->languageComboBox->blockSignals(true);
				ui->languageComboBox->setCurrentIndex(idx);
				ui->languageComboBox->blockSignals(false);
			}
		}

		if (ui->presetDebugBtn) ui->presetDebugBtn->setText(tr("Enable Core Debug"));
		if (ui->resetRulesBtn) ui->resetRulesBtn->setText(tr("Reset Rules"));
		if (ui->restoreDefaultsBtn) ui->restoreDefaultsBtn->setText(tr("恢复默认值"));
	}
}
void SettingsDialog::saveAuthConfig()
{
	const QString username = ui->usernameEdit->text().trimmed();
	const QString password = ui->passwordEdit->text();

	if (username.isEmpty()) {
		// 用户名为空 → 完全禁用认证
		m_settings->remove("Server/username");
		m_settings->remove("Server/password");
		m_cachedPassword.clear();
	} else {
		m_settings->setString("Server/username", username);
		if (password.isEmpty()) {
			m_settings->remove("Server/password");
			m_cachedPassword.clear();
		} else {
			const QString encrypted = PasswordCrypto::encrypt(username, password);
			m_settings->setString("Server/password", encrypted);
			m_cachedPassword = password;
		}
	}

	qCDebug(lcUISettingsDialog) << "SettingsDialog: auth config saved";
}

void SettingsDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	// 每次显示对话框时重新应用标题栏主题
	//（hide()/exec() 返回后 Windows 可能重置 DWM 属性）
	TitleBarTheme::apply(this, IconThemeProvider::isDarkMode());
}

void SettingsDialog::closeEvent(QCloseEvent* event)
{
	// 关闭前校验互斥规则：用户名和密码必须同时为空或同时有值
	const QString username = ui->usernameEdit->text().trimmed();
	const QString password = ui->passwordEdit->text();
	const bool hasUsername = !username.isEmpty();
	const bool hasPassword = !password.isEmpty();

	if (hasUsername != hasPassword) {
		QMessageBox::warning(this,
			tr("认证配置不完整"),
			tr("用户名和密码必须同时填写或同时留空以跳过认证。"));
		event->ignore();
		return;
	}

	QDialog::closeEvent(event);
}

void SettingsDialog::hideEvent(QHideEvent* event)
{
	// 退出前持久化认证配置（editingFinished 仅内存缓存，此处统一写入）
	saveAuthConfig();
	// 立即刷盘所有设置变更
	m_settings->save();
	QDialog::hideEvent(event);
}
