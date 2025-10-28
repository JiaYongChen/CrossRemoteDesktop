#include "SettingsDialog.h"
#include "ui_SettingsDialog.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#include <QtWidgets/QTextEdit>
#include <QtCore/QStandardPaths>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QFontDialog>
#include <QtWidgets/QStyleFactory>
#include <QtCore/QDir>
#include <QtCore/QProcess>
#include <QtGui/QDesktopServices>
#include <QtCore/QUrl>
#include <QtCore/QSettings>
#include <QtWidgets/QApplication>
#include <QtGui/QScreen>
#include <QtCore/QTimer>
#include <QtWidgets/QProgressDialog>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QCryptographicHash>
#include "common/core/config/Config.h"
#include "common/core/logging/LoggingCategories.h"

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
    , m_settings(new QSettings())
    , ui(new Ui::SettingsDialog)
    , m_settingsChanged(false) {
    ui->setupUi(this);
    setupUI();
    setupConnections();
    loadSettings();
}

SettingsDialog::~SettingsDialog() {
    delete ui;
}

void SettingsDialog::setupUI() {
    // 获取UI文件中的组件引用
    m_categoryListWidget = ui->categoryListWidget;
    m_settingsStackedWidget = ui->settingsStackedWidget;

    // 获取按钮盒引用
    m_buttonBox = ui->buttonBox;

    // 获取标准按钮
    m_okButton = m_buttonBox->button(QDialogButtonBox::Ok);
    m_cancelButton = m_buttonBox->button(QDialogButtonBox::Cancel);
    m_applyButton = m_buttonBox->button(QDialogButtonBox::Apply);
    m_defaultsButton = m_buttonBox->button(QDialogButtonBox::RestoreDefaults);

    // 创建额外的按钮
    m_resetButton = new QPushButton(tr("重置"), nullptr);
    m_importButton = new QPushButton(tr("导入"), nullptr);
    m_exportButton = new QPushButton(tr("导出"), nullptr);

    // 将额外按钮添加到按钮盒
    m_buttonBox->addButton(m_resetButton, QDialogButtonBox::ActionRole);
    m_buttonBox->addButton(m_importButton, QDialogButtonBox::ActionRole);
    m_buttonBox->addButton(m_exportButton, QDialogButtonBox::ActionRole);

    // 获取各个页面的组件引用
    setupGeneralPageComponents();
    setupConnectionPageComponents();
    setupDisplayPageComponents();
    setupAudioPageComponents();
    setupSecurityPageComponents();
    setupAdvancedPageComponents();

    // 连接分类列表的选择信号
    connect(m_categoryListWidget, &QListWidget::currentRowChanged,
        m_settingsStackedWidget, &QStackedWidget::setCurrentIndex);

    // 设置默认选中第一项
    m_categoryListWidget->setCurrentRow(0);

    // 更新列表
    updateLanguageList();
    updateThemeList();
    updateAudioDeviceList();
}

void SettingsDialog::setupConnections() {
    // 连接按钮盒的信号
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    // 连接Apply按钮（如果存在）
    if ( QPushButton* applyButton = m_buttonBox->button(QDialogButtonBox::Apply) ) {
        connect(applyButton, &QPushButton::clicked, this, &SettingsDialog::onApplyClicked);
    }

    // 连接端口设置信号
    if ( ui->defaultPortSpinBox ) {
        connect(ui->defaultPortSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::onDefaultPortChanged);
    }

    // 连接帧率设置信号
    if ( ui->frameRateSpinBox ) {
        connect(ui->frameRateSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::onFrameRateChanged);
    }

    // 连接缩放模式设置信号
    if ( ui->scalingModeComboBox ) {
        connect(ui->scalingModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onScalingModeChanged);
    }

    // 高级-日志：规则编辑变更
    if ( QTextEdit* rules = findChild<QTextEdit*>("logRulesTextEdit") ) {
        connect(rules, &QTextEdit::textChanged, this, &SettingsDialog::onSettingChanged);
    }
    // 高级-日志：级别变更
    if ( ui->logLevelComboBox ) {
        connect(ui->logLevelComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onLoggingLevelChanged);
    }
}

void SettingsDialog::setupGeneralPageComponents() {
    // 获取UI文件中的组件引用
    m_languageCombo = ui->languageComboBox;
    m_startWithSystemCheck = ui->autoStartCheckBox;
    m_minimizeToTrayCheck = ui->minimizeToTrayCheckBox;
    m_checkUpdatesCheck = ui->autoUpdateCheckBox;

    // 注意：UI文件中可能没有主题选择和通知设置，需要检查
    // 如果UI文件中没有这些组件，我们可以创建它们或者注释掉相关代码
    m_showNotificationsCheck = nullptr; // UI文件中暂时没有此组件
    m_themeCombo = nullptr; // UI文件中暂时没有此组件
}

void SettingsDialog::createGeneralTab() {
    // 这个函数现在不需要了，因为UI是通过.ui文件定义的
    // 保留空实现以避免编译错误
}

void SettingsDialog::setupConnectionPageComponents() {
    // 获取UI文件中的组件引用
    m_defaultPortSpinBox = ui->defaultPortSpinBox;
    m_connectionTimeoutSpinBox = ui->defaultTimeoutSpinBox;
    m_autoReconnectCheck = ui->enableAutoReconnectCheckBox;
    m_reconnectIntervalSpinBox = ui->retryIntervalSpinBox;
    m_maxReconnectAttemptsSpinBox = ui->maxRetriesSpinBox;

    // 这些组件在UI文件中可能没有定义，设置为nullptr
    m_enableUPnPCheck = nullptr;
    m_proxyHostEdit = nullptr;
    m_proxyPortSpinBox = nullptr;
    m_proxyUsernameEdit = nullptr;
    m_proxyPasswordEdit = nullptr;
}

void SettingsDialog::createConnectionTab() {
    // 这个函数现在不需要了，因为UI是通过.ui文件定义的
    // 保留空实现以避免编译错误
}

void SettingsDialog::setupDisplayPageComponents() {
    // 获取UI文件中的组件引用
    m_colorDepthCombo = ui->defaultColorDepthComboBox;
    m_enableCursorCheck = ui->showCursorCheckBox;
    m_frameRateSpinBox = ui->frameRateSpinBox;
    m_scalingModeCombo = ui->scalingModeComboBox;
    // m_qualitySlider = ui->defaultQualitySlider;  // Component not found
    // m_adaptiveQualityCheck = ui->enableAdaptiveQualityCheckBox;  // Component not found
    // m_fullScreenCheck = ui->defaultFullScreenCheckBox;  // Component not found

    // 这些组件在UI文件中没有定义，设置为nullptr

    m_enableWallpaperCheck = nullptr;
    m_enableAnimationsCheck = nullptr;
    m_enableFontSmoothingCheck = nullptr;
}

void SettingsDialog::createDisplayTab() {
    // 这个函数现在不需要了，因为UI是通过.ui文件定义的
    // 保留空实现以避免编译错误
}

void SettingsDialog::setupAudioPageComponents() {
    // 获取UI文件中的组件引用
    m_enableAudioCheck = ui->enableAudioCheckBox;
    m_audioQualityCombo = ui->audioQualityComboBox;
    // m_audioBufferSpin = ui->audioBufferSpinBox;  // Component not found

    // 这些组件在UI文件中没有定义，设置为nullptr
    m_audioDeviceCombo = nullptr;
    m_audioVolumeSlider = nullptr;
    m_audioVolumeLabel = nullptr;
    m_enableMicrophoneCheck = nullptr;
    m_microphoneDeviceCombo = nullptr;
    m_microphoneVolumeSlider = nullptr;
    m_microphoneVolumeLabel = nullptr;
}

void SettingsDialog::createAudioTab() {
    // 这个函数现在不需要了，因为UI是通过.ui文件定义的
    // 保留空实现以避免编译错误
}

void SettingsDialog::setupSecurityPageComponents() {
    // 获取UI文件中的组件引用
    m_enableEncryptionCheck = ui->defaultEncryptionCheckBox;
    m_requirePasswordCheck = ui->savePasswordsCheckBox;
    m_sessionTimeoutSpinBox = ui->defaultTimeoutSpinBox;

    // 这些组件在UI文件中可能没有定义，设置为nullptr
    m_encryptionMethodCombo = nullptr;
    m_passwordLengthSpinBox = nullptr;
    m_passwordComplexityCheck = nullptr;
    m_logSecurityEventsCheck = nullptr;
    m_trustedHostsEdit = nullptr;
}

void SettingsDialog::createSecurityTab() {
    // 这个函数现在不需要了，因为UI是通过.ui文件定义的
    // 保留空实现以避免编译错误
}

void SettingsDialog::setupAdvancedPageComponents() {
    // 获取UI文件中的组件引用
    m_loggingLevelCombo = ui->logLevelComboBox;
    // 新增：日志规则编辑器（可能不存在，空指针容错）
    m_loggingRulesEdit = findChild<QTextEdit*>("logRulesTextEdit");
    // m_browseLogPathButton = ui->browseLogPathButton;  // Component not found in UI

    // 这些组件在UI文件中可能没有定义，设置为nullptr
    m_logFilePathEdit = nullptr;
    m_maxLogFileSizeSpinBox = nullptr;
    m_maxLogFilesSpinBox = nullptr;
    m_performanceUpdateIntervalSpinBox = nullptr;
    m_enableDebugModeCheck = nullptr;
    m_customSettingsEdit = nullptr;

    // 动态添加“规则预设”与“恢复默认规则”按钮到日志表单布局底部
    if ( ui->loggingFormLayout ) {
        QWidget* buttonBar = new QWidget(ui->advancedPage);
        auto* h = new QHBoxLayout(buttonBar);
        h->setContentsMargins(0, 0, 0, 0);
        QPushButton* presetBtn = new QPushButton(tr("Enable Core Debug"), buttonBar);
        QPushButton* resetBtn = new QPushButton(tr("Reset Rules"), buttonBar);
        h->addWidget(presetBtn);
        h->addWidget(resetBtn);
        h->addStretch();
        // 放在新的一行，横跨两列
        ui->loggingFormLayout->setWidget(2, QFormLayout::SpanningRole, buttonBar);

        // 预设核心调试规则（不直接应用，仅填入文本并标记更改）
        connect(presetBtn, &QPushButton::clicked, this, [this]() {
            if ( !m_loggingRulesEdit ) {
                m_loggingRulesEdit = findChild<QTextEdit*>("logRulesTextEdit");
            }
            if ( m_loggingRulesEdit ) {
                const QString coreRules =
                    "app.debug=true\n"
                    "server*.debug=true\n"
                    "client*.debug=true\n"
                    "core.*.debug=true\n"
                    "qt.network.ssl.warning=false";
                m_loggingRulesEdit->setPlainText(coreRules);
                onSettingChanged();
            }
        });

        // 恢复默认（清空规则，由 Qt 默认规则或环境变量生效）
        connect(resetBtn, &QPushButton::clicked, this, [this]() {
            if ( !m_loggingRulesEdit ) {
                m_loggingRulesEdit = findChild<QTextEdit*>("logRulesTextEdit");
            }
            if ( m_loggingRulesEdit ) {
                m_loggingRulesEdit->clear();
                onSettingChanged();
            }
        });
    }
}

void SettingsDialog::createAdvancedTab() {
    // 这个函数现在不需要了，因为UI是通过.ui文件定义的
    // 保留空实现以避免编译错误
}

void SettingsDialog::updateLanguageList() {
    if ( m_languageCombo ) {
        m_languageCombo->clear();
        m_languageCombo->addItems({ tr("英语"), tr("中文"), tr("日语"), tr("韩语") });
    }
}

void SettingsDialog::updateThemeList() {
    if ( m_themeCombo ) {
        m_themeCombo->clear();
        m_themeCombo->addItems({ tr("浅色"), tr("深色"), tr("自动") });
    }
}

void SettingsDialog::updateAudioDeviceList() {
    if ( m_audioDeviceCombo ) {
        m_audioDeviceCombo->clear();
        m_audioDeviceCombo->addItems({ tr("默认"), tr("系统音频") });
    }

    if ( m_microphoneDeviceCombo ) {
        m_microphoneDeviceCombo->clear();
        m_microphoneDeviceCombo->addItems({ tr("默认"), tr("系统麦克风") });
    }
}

void SettingsDialog::loadSettings() {
    m_settings->beginGroup("General");
    m_generalSettings.language = m_settings->value("language", "English").toString();
    m_generalSettings.theme = m_settings->value("theme", "Light").toString();
    m_generalSettings.startWithSystem = m_settings->value("startWithSystem", false).toBool();
    m_generalSettings.minimizeToTray = m_settings->value("minimizeToTray", false).toBool();
    m_generalSettings.showNotifications = m_settings->value("showNotifications", true).toBool();
    m_generalSettings.checkUpdates = m_settings->value("checkUpdates", true).toBool();
    m_settings->endGroup();

    // Logging 设置
    m_settings->beginGroup("Logging");
    m_advancedSettings.loggingLevel = m_settings->value("level", "info").toString();
    m_advancedSettings.loggingRules = m_settings->value("rules", "").toString();
    m_settings->endGroup();

    m_settings->beginGroup("Connection");
    m_connectionSettings.defaultPort = m_settings->value("defaultPort", 3389).toInt();
    m_connectionSettings.connectionTimeout = m_settings->value("connectionTimeout", 30).toInt();
    m_connectionSettings.autoReconnect = m_settings->value("autoReconnect", false).toBool();
    m_connectionSettings.reconnectInterval = m_settings->value("reconnectInterval", 5).toInt();
    m_connectionSettings.maxReconnectAttempts = m_settings->value("maxReconnectAttempts", 3).toInt();
    m_connectionSettings.enableUPnP = m_settings->value("enableUPnP", false).toBool();
    m_connectionSettings.proxyHost = m_settings->value("proxyHost", "").toString();
    m_connectionSettings.proxyPort = m_settings->value("proxyPort", 8080).toInt();
    m_connectionSettings.proxyUsername = m_settings->value("proxyUsername", "").toString();
    m_connectionSettings.proxyPassword = m_settings->value("proxyPassword", "").toString();
    m_settings->endGroup();

    m_settings->beginGroup("Display");
    m_displaySettings.frameRate = m_settings->value("frameRate", 60).toInt();
    m_displaySettings.colorDepth = m_settings->value("colorDepth", "32-bit").toString();
    m_displaySettings.enableCursor = m_settings->value("enableCursor", true).toBool();
    m_displaySettings.enableWallpaper = m_settings->value("enableWallpaper", false).toBool();
    m_displaySettings.enableAnimations = m_settings->value("enableAnimations", false).toBool();
    m_displaySettings.enableFontSmoothing = m_settings->value("enableFontSmoothing", true).toBool();
    m_displaySettings.scalingMode = m_settings->value("scalingMode", "FitToWindow").toString();
    m_settings->endGroup();

    // 应用设置到UI
    applySettingsToUI();
}

void SettingsDialog::saveSettings() {
    if ( !m_settingsChanged ) {
        return;
    }

    // 从UI获取设置
    getSettingsFromUI();

    m_settings->beginGroup("General");
    m_settings->setValue("language", m_generalSettings.language);
    m_settings->setValue("theme", m_generalSettings.theme);
    m_settings->setValue("startWithSystem", m_generalSettings.startWithSystem);
    m_settings->setValue("minimizeToTray", m_generalSettings.minimizeToTray);
    m_settings->setValue("showNotifications", m_generalSettings.showNotifications);
    m_settings->setValue("checkUpdates", m_generalSettings.checkUpdates);
    m_settings->endGroup();

    // 保存 Logging 设置
    m_settings->beginGroup("Logging");
    m_settings->setValue("level", m_advancedSettings.loggingLevel);
    m_settings->setValue("rules", m_advancedSettings.loggingRules);
    m_settings->endGroup();

    m_settings->beginGroup("Connection");
    m_settings->setValue("defaultPort", m_connectionSettings.defaultPort);
    m_settings->setValue("connectionTimeout", m_connectionSettings.connectionTimeout);
    m_settings->setValue("autoReconnect", m_connectionSettings.autoReconnect);
    m_settings->setValue("reconnectInterval", m_connectionSettings.reconnectInterval);
    m_settings->setValue("maxReconnectAttempts", m_connectionSettings.maxReconnectAttempts);
    m_settings->setValue("enableUPnP", m_connectionSettings.enableUPnP);
    m_settings->setValue("proxyHost", m_connectionSettings.proxyHost);
    m_settings->setValue("proxyPort", m_connectionSettings.proxyPort);
    m_settings->setValue("proxyUsername", m_connectionSettings.proxyUsername);
    m_settings->setValue("proxyPassword", m_connectionSettings.proxyPassword);
    m_settings->endGroup();

    m_settings->beginGroup("Display");
    m_settings->setValue("frameRate", m_displaySettings.frameRate);
    m_settings->setValue("colorDepth", m_displaySettings.colorDepth);
    m_settings->setValue("enableCursor", m_displaySettings.enableCursor);
    m_settings->setValue("enableWallpaper", m_displaySettings.enableWallpaper);
    m_settings->setValue("enableAnimations", m_displaySettings.enableAnimations);
    m_settings->setValue("enableFontSmoothing", m_displaySettings.enableFontSmoothing);
    m_settings->setValue("scalingMode", m_displaySettings.scalingMode);
    m_settings->endGroup();

    m_settings->sync();
    m_settingsChanged = false;
}

void SettingsDialog::applySettingsToUI() {
    // 通用设置
    if ( m_languageCombo ) {
        int langIndex = m_languageCombo->findText(m_generalSettings.language);
        if ( langIndex >= 0 ) m_languageCombo->setCurrentIndex(langIndex);
    }

    if ( m_themeCombo ) {
        int themeIndex = m_themeCombo->findText(m_generalSettings.theme);
        if ( themeIndex >= 0 ) m_themeCombo->setCurrentIndex(themeIndex);
    }

    if ( m_startWithSystemCheck ) m_startWithSystemCheck->setChecked(m_generalSettings.startWithSystem);
    if ( m_minimizeToTrayCheck ) m_minimizeToTrayCheck->setChecked(m_generalSettings.minimizeToTray);
    if ( m_showNotificationsCheck ) m_showNotificationsCheck->setChecked(m_generalSettings.showNotifications);
    if ( m_checkUpdatesCheck ) m_checkUpdatesCheck->setChecked(m_generalSettings.checkUpdates);

    // 连接设置
    if ( m_defaultPortSpinBox ) m_defaultPortSpinBox->setValue(m_connectionSettings.defaultPort);
    if ( m_connectionTimeoutSpinBox ) m_connectionTimeoutSpinBox->setValue(m_connectionSettings.connectionTimeout);
    if ( m_autoReconnectCheck ) m_autoReconnectCheck->setChecked(m_connectionSettings.autoReconnect);
    if ( m_reconnectIntervalSpinBox ) m_reconnectIntervalSpinBox->setValue(m_connectionSettings.reconnectInterval);
    if ( m_maxReconnectAttemptsSpinBox ) m_maxReconnectAttemptsSpinBox->setValue(m_connectionSettings.maxReconnectAttempts);
    if ( m_enableUPnPCheck ) m_enableUPnPCheck->setChecked(m_connectionSettings.enableUPnP);
    if ( m_proxyHostEdit ) m_proxyHostEdit->setText(m_connectionSettings.proxyHost);
    if ( m_proxyPortSpinBox ) m_proxyPortSpinBox->setValue(m_connectionSettings.proxyPort);
    if ( m_proxyUsernameEdit ) m_proxyUsernameEdit->setText(m_connectionSettings.proxyUsername);
    if ( m_proxyPasswordEdit ) m_proxyPasswordEdit->setText(m_connectionSettings.proxyPassword);

    // 显示设置
    if ( m_frameRateSpinBox ) m_frameRateSpinBox->setValue(m_displaySettings.frameRate);
    if ( m_enableCursorCheck ) m_enableCursorCheck->setChecked(m_displaySettings.enableCursor);

    // 设置缩放模式
    if ( m_scalingModeCombo ) {
        int scalingIndex = 0;
        if ( m_displaySettings.scalingMode == "FitToWindow" ) scalingIndex = 0;
        else if ( m_displaySettings.scalingMode == "ActualSize" ) scalingIndex = 1;
        else if ( m_displaySettings.scalingMode == "FillWindow" ) scalingIndex = 2;
        m_scalingModeCombo->setCurrentIndex(scalingIndex);
    }

    // 高级-日志：级别
    if ( m_loggingLevelCombo ) {
        // 将常见级别映射中文→英文值
        const QString lvl = m_advancedSettings.loggingLevel.toLower();
        int idx = 0; // 默认错误/警告/信息/调试 中，匹配到最近
        if ( lvl == "error" || lvl == "错误" ) idx = 0;
        else if ( lvl == "warning" || lvl == "警告" ) idx = 1;
        else if ( lvl == "info" || lvl == "信息" ) idx = 2;
        else if ( lvl == "debug" || lvl == "调试" ) idx = 3;
        m_loggingLevelCombo->setCurrentIndex(idx);
    }
    if ( m_loggingRulesEdit ) {
        m_loggingRulesEdit->setPlainText(m_advancedSettings.loggingRules);
    }
}

void SettingsDialog::getSettingsFromUI() {
    // 通用设置 - 添加空指针检查
    if ( m_languageCombo ) m_generalSettings.language = m_languageCombo->currentText();
    if ( m_themeCombo ) m_generalSettings.theme = m_themeCombo->currentText();
    if ( m_startWithSystemCheck ) m_generalSettings.startWithSystem = m_startWithSystemCheck->isChecked();
    if ( m_minimizeToTrayCheck ) m_generalSettings.minimizeToTray = m_minimizeToTrayCheck->isChecked();
    if ( m_showNotificationsCheck ) m_generalSettings.showNotifications = m_showNotificationsCheck->isChecked();
    if ( m_checkUpdatesCheck ) m_generalSettings.checkUpdates = m_checkUpdatesCheck->isChecked();

    // 连接设置 - 添加空指针检查
    if ( m_defaultPortSpinBox ) m_connectionSettings.defaultPort = m_defaultPortSpinBox->value();
    if ( m_connectionTimeoutSpinBox ) m_connectionSettings.connectionTimeout = m_connectionTimeoutSpinBox->value();
    if ( m_autoReconnectCheck ) m_connectionSettings.autoReconnect = m_autoReconnectCheck->isChecked();
    if ( m_reconnectIntervalSpinBox ) m_connectionSettings.reconnectInterval = m_reconnectIntervalSpinBox->value();
    if ( m_maxReconnectAttemptsSpinBox ) m_connectionSettings.maxReconnectAttempts = m_maxReconnectAttemptsSpinBox->value();
    if ( m_enableUPnPCheck ) m_connectionSettings.enableUPnP = m_enableUPnPCheck->isChecked();
    if ( m_proxyHostEdit ) m_connectionSettings.proxyHost = m_proxyHostEdit->text();
    if ( m_proxyPortSpinBox ) m_connectionSettings.proxyPort = m_proxyPortSpinBox->value();
    if ( m_proxyUsernameEdit ) m_connectionSettings.proxyUsername = m_proxyUsernameEdit->text();
    if ( m_proxyPasswordEdit ) m_connectionSettings.proxyPassword = m_proxyPasswordEdit->text();

    // 显示设置 - 添加空指针检查
    if ( m_frameRateSpinBox ) m_displaySettings.frameRate = m_frameRateSpinBox->value();
    if ( m_enableCursorCheck ) m_displaySettings.enableCursor = m_enableCursorCheck->isChecked();

    // 缩放模式设置
    if ( m_scalingModeCombo ) {
        int scalingIndex = m_scalingModeCombo->currentIndex();
        switch ( scalingIndex ) {
            case 0: m_displaySettings.scalingMode = "FitToWindow"; break;
            case 1: m_displaySettings.scalingMode = "ActualSize"; break;
            case 2: m_displaySettings.scalingMode = "FillWindow"; break;
            default: m_displaySettings.scalingMode = "FitToWindow"; break;
        }
    }

    // 高级-日志
    if ( m_loggingLevelCombo ) {
        switch ( m_loggingLevelCombo->currentIndex() ) {
            case 0: m_advancedSettings.loggingLevel = "error"; break;
            case 1: m_advancedSettings.loggingLevel = "warning"; break;
            case 2: m_advancedSettings.loggingLevel = "info"; break;
            case 3: m_advancedSettings.loggingLevel = "debug"; break;
            default: m_advancedSettings.loggingLevel = "info"; break;
        }
    }
    if ( m_loggingRulesEdit ) {
        m_advancedSettings.loggingRules = m_loggingRulesEdit->toPlainText();
    }
}

bool SettingsDialog::validateSettings() {
    // 验证端口范围 - 添加空指针检查
    if ( m_defaultPortSpinBox && (m_defaultPortSpinBox->value() < 1 || m_defaultPortSpinBox->value() > 65535) ) {
        showValidationError(tr("无效的端口号"));
        return false;
    }

    return true;
}

void SettingsDialog::showValidationError(const QString& message) {
    QMessageBox::warning(nullptr, tr("验证错误"), message);
}

void SettingsDialog::accept() {
    if ( validateSettings() ) {
        applySettings();
        QDialog::accept(); // 调用基类方法关闭对话框
    }
}

void SettingsDialog::reject() {
    // 如果有未保存的更改，可以在这里提示用户
    QDialog::reject(); // 调用基类方法关闭对话框
}

void SettingsDialog::onApplyClicked() {
    if ( validateSettings() ) {
        applySettings();
    }
}

void SettingsDialog::onResetClicked() {
    loadSettings();
    m_settingsChanged = false;
}

void SettingsDialog::onDefaultsClicked() {
    resetToDefaults();
}

void SettingsDialog::onImportClicked() {
    QString fileName = QFileDialog::getOpenFileName(nullptr, tr("导入设置"), "", tr("设置文件 (*.ini)"));
    if ( !fileName.isEmpty() ) {
        // 导入设置逻辑
    }
}

void SettingsDialog::onExportClicked() {
    QString fileName = QFileDialog::getSaveFileName(nullptr, tr("导出设置"), "", tr("设置文件 (*.ini)"));
    if ( !fileName.isEmpty() ) {
        // 导出设置逻辑
    }
}

void SettingsDialog::applySettings() {
    getSettingsFromUI();
    saveSettings();

    // 应用到全局 Logger / Config
    Config::instance()->setValue("level", m_advancedSettings.loggingLevel, Config::Logging);
    Config::instance()->setValue("rules", m_advancedSettings.loggingRules, Config::Logging);
    // 优先环境变量，不覆盖环境变量，只应用配置规则（若未设置环境变量）
    const QByteArray envRules = qgetenv("QT_LOGGING_RULES");
    if ( envRules.isEmpty() ) {
        if ( !m_advancedSettings.loggingRules.trimmed().isEmpty() ) {
            QLoggingCategory::setFilterRules(m_advancedSettings.loggingRules);
        }
    }
    // 注意：QLoggingCategory不支持动态设置日志级别，需要通过环境变量或过滤规则设置
}

void SettingsDialog::resetToDefaults() {
    // 重置为默认值
    m_languageCombo->setCurrentIndex(0);
    m_themeCombo->setCurrentIndex(0);
    m_startWithSystemCheck->setChecked(false);
    m_minimizeToTrayCheck->setChecked(false);
    m_showNotificationsCheck->setChecked(true);
    m_checkUpdatesCheck->setChecked(true);
    m_defaultPortSpinBox->setValue(3389);
    m_connectionTimeoutSpinBox->setValue(30);
    m_autoReconnectCheck->setChecked(false);
    m_reconnectIntervalSpinBox->setValue(5);
    m_maxReconnectAttemptsSpinBox->setValue(3);
    m_enableUPnPCheck->setChecked(false);
    m_proxyHostEdit->clear();
    m_proxyPortSpinBox->setValue(8080);
    m_proxyUsernameEdit->clear();
    m_proxyPasswordEdit->clear();

    m_settingsChanged = true;
}

// 通用设置变更处理函数
void SettingsDialog::onSettingChanged() {
    m_settingsChanged = true;
}

// 槽函数实现 - 使用通用处理函数
void SettingsDialog::onLanguageChanged(int index) { Q_UNUSED(index); onSettingChanged(); }
void SettingsDialog::onThemeChanged(int index) { Q_UNUSED(index); onSettingChanged(); }
void SettingsDialog::onStartupBehaviorChanged(bool checked) { Q_UNUSED(checked); onSettingChanged(); }
void SettingsDialog::onDefaultPortChanged(int value) { Q_UNUSED(value); onSettingChanged(); }
void SettingsDialog::onConnectionTimeoutChanged(int value) { Q_UNUSED(value); onSettingChanged(); }
void SettingsDialog::onAutoReconnectChanged(bool checked) { Q_UNUSED(checked); onSettingChanged(); }
void SettingsDialog::onFrameRateChanged(int value) { Q_UNUSED(value); onSettingChanged(); }
void SettingsDialog::onScalingModeChanged(int index) { Q_UNUSED(index); onSettingChanged(); }
void SettingsDialog::onAudioEnabledChanged(bool checked) { Q_UNUSED(checked); onSettingChanged(); }
void SettingsDialog::onAudioQualityChanged(int index) { Q_UNUSED(index); onSettingChanged(); }
void SettingsDialog::onAudioDeviceChanged(int index) { Q_UNUSED(index); onSettingChanged(); }
void SettingsDialog::onEncryptionChanged(bool checked) { Q_UNUSED(checked); onSettingChanged(); }
void SettingsDialog::onPasswordPolicyChanged(int index) { Q_UNUSED(index); onSettingChanged(); }
void SettingsDialog::onSessionTimeoutChanged(int value) { Q_UNUSED(value); onSettingChanged(); }
void SettingsDialog::onLoggingLevelChanged(int index) { Q_UNUSED(index); onSettingChanged(); }
void SettingsDialog::onLogFilePathChanged(const QString& text) { Q_UNUSED(text); onSettingChanged(); }