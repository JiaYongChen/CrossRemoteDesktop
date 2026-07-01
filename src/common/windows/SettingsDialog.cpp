#include "SettingsDialog.h"
#include "ui_SettingsDialog.h"
#include "common/core/config/Config.h"
#include "common/core/TranslationUtils.h"
#include "common/core/logging/LoggingCategories.h"
#include "common/core/config/UiConstants.h"
#include "common/core/crypto/PasswordCrypto.h"
#include "common/core/theme/IconThemeProvider.h"

#include <QtCore/QSettings>
#include <QtCore/QEvent>
#include <QtCore/QVariant>
#include <QtCore/QByteArray>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QPushButton>
#include <QtGui/QIcon>

SettingsDialog::SettingsDialog(QWidget* parent)
	: QDialog(parent)
	, ui(new Ui::SettingsDialog)
	, m_settings(new QSettings())
{
	ui->setupUi(this);
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

	const QString currentLang = Config::instance()->value("language", "zh_CN").toString();
	const int idx = ui->languageComboBox->findData(QVariant(currentLang));
	if (idx >= 0) ui->languageComboBox->setCurrentIndex(idx);

	ui->languageComboBox->blockSignals(false);
}

void SettingsDialog::loadSettings()
{
	// 常规
	const bool autoStart = m_settings->value("General/startWithSystem", false).toBool();
	ui->autoStartCheckBox->setChecked(autoStart);

	// 通信
	const int listenPort = m_settings->value("Server/listenPort", UIConstants::DEFAULT_SERVER_PORT).toInt();
	ui->listenPortSpinBox->setValue(listenPort);

	const QString username = m_settings->value("Server/username").toString();
	ui->usernameEdit->setText(username);

	const QString encryptedPassword = m_settings->value("Server/password").toString();
	m_cachedPassword = PasswordCrypto::decrypt(username, encryptedPassword);
	ui->passwordEdit->setText(m_cachedPassword);

	// 高级
	const QString logLevel = m_settings->value("Logging/level", "info").toString().toLower();
	int levelIdx = 2; // default: info
	if (logLevel == "error" || logLevel == QStringLiteral("错误")) levelIdx = 0;
	else if (logLevel == "warning" || logLevel == QStringLiteral("警告")) levelIdx = 1;
	else if (logLevel == "debug" || logLevel == QStringLiteral("调试")) levelIdx = 3;
	ui->logLevelComboBox->setCurrentIndex(levelIdx);

	const QString logRules = m_settings->value("Logging/rules").toString();
	ui->logRulesTextEdit->setPlainText(logRules);
}

// ===== 即时生效槽 =====

void SettingsDialog::onLanguageChanged(int index)
{
	const QString lang = ui->languageComboBox->itemData(index).toString();
	if (lang.isEmpty()) return;

	Config::instance()->setValue("language", lang, Config::General);
	m_settings->setValue("General/language", lang);
	switchTranslation(*qApp, lang);
	qCInfo(lcUISettingsDialog) << "SettingsDialog: language switched to" << lang;
}

void SettingsDialog::onAutoStartChanged(bool checked)
{
	m_settings->setValue("General/startWithSystem", checked);
	qCDebug(lcUISettingsDialog) << "SettingsDialog: auto start set to" << checked;
}

void SettingsDialog::onListenPortChanged(int value)
{
	m_settings->setValue("Server/listenPort", value);
	qCDebug(lcUISettingsDialog) << "SettingsDialog: listen port set to" << value;
}

void SettingsDialog::onUsernameChanged()
{
	const QString oldUsername = m_settings->value("Server/username").toString();
	const QString newUsername = ui->usernameEdit->text();

	if (newUsername == oldUsername) return;

	// 如果已有密码，用旧用户名解密 → 新用户名重新加密
	const QString oldEncrypted = m_settings->value("Server/password").toString();
	if (!oldEncrypted.isEmpty() && !m_cachedPassword.isEmpty()) {
		const QString newEncrypted = PasswordCrypto::encrypt(newUsername, m_cachedPassword);
		m_settings->setValue("Server/password", newEncrypted);
	}

	m_settings->setValue("Server/username", newUsername);
	qCDebug(lcUISettingsDialog) << "SettingsDialog: username changed";
}

void SettingsDialog::onPasswordChanged()
{
	const QString newPassword = ui->passwordEdit->text();
	if (newPassword == m_cachedPassword) return;

	m_cachedPassword = newPassword;
	const QString username = m_settings->value("Server/username").toString();

	if (newPassword.isEmpty()) {
		m_settings->remove("Server/password");
	} else {
		const QString encrypted = PasswordCrypto::encrypt(username, newPassword);
		m_settings->setValue("Server/password", encrypted);
	}
	qCDebug(lcUISettingsDialog) << "SettingsDialog: password updated";
}

void SettingsDialog::onLogLevelChanged(int index)
{
	static const char* levels[] = {"error", "warning", "info", "debug"};
	if (index < 0 || index > 3) return;

	m_settings->setValue("Logging/level", levels[index]);
	Config::instance()->setValue("level", levels[index], Config::Logging);
	qCDebug(lcUISettingsDialog) << "SettingsDialog: log level set to" << levels[index];
}

void SettingsDialog::onLogRulesChanged()
{
	const QString rules = ui->logRulesTextEdit->toPlainText();
	m_settings->setValue("Logging/rules", rules);
	Config::instance()->setValue("rules", rules, Config::Logging);
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

	ui->autoStartCheckBox->setChecked(false);
	onAutoStartChanged(false);

	ui->listenPortSpinBox->setValue(UIConstants::DEFAULT_SERVER_PORT);
	onListenPortChanged(UIConstants::DEFAULT_SERVER_PORT);

	ui->usernameEdit->clear();
	ui->passwordEdit->clear();
	m_cachedPassword.clear();
	m_settings->remove("Server/username");
	m_settings->remove("Server/password");

	ui->logLevelComboBox->setCurrentIndex(2);
	onLogLevelChanged(2);

	ui->logRulesTextEdit->clear();
	m_settings->remove("Logging/rules");
	Config::instance()->remove("rules", Config::Logging);

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
