#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QtWidgets/QDialog>
#include <QtCore/QString>

QT_BEGIN_NAMESPACE
namespace Ui { class SettingsDialog; }
QT_END_NAMESPACE

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog();

protected:
    void accept() override;
    void reject() override;

private slots:
    void onApplyClicked();
    void onResetClicked();
    void onDefaultsClicked();
    void onImportClicked();
    void onExportClicked();

    // 通用设置变更处理
    void onSettingChanged();

    // 常规设置
    void onLanguageChanged(int index);
    void onThemeChanged(int index);
    void onStartupBehaviorChanged(bool checked);

    // 连接设置
    void onDefaultPortChanged(int value);
    void onConnectionTimeoutChanged(int value);
    void onAutoReconnectChanged(bool checked);

    // 显示设置
    void onFrameRateChanged(int value);
    void onCaptureQualityChanged(int index);
    void onScalingModeChanged(int index);

    // 音频设置
    void onAudioEnabledChanged(bool checked);
    void onAudioQualityChanged(int index);
    void onAudioDeviceChanged(int index);

    // 安全设置
    void onEncryptionChanged(bool checked);
    void onPasswordPolicyChanged(int index);
    void onSessionTimeoutChanged(int value);

    // 高级设置
    void onLoggingLevelChanged(int index);
    void onLogFilePathChanged(const QString& text);

private:
    void setupUI();
    void setupConnections();
    void loadSettings();
    void saveSettings();
    void applySettings();
    void resetToDefaults();
    void applySettingsToUI();
    void getSettingsFromUI();
    bool validateSettings();
    void showValidationError(const QString& message);

    void createGeneralTab();
    void createConnectionTab();
    void createDisplayTab();
    void createAudioTab();
    void createSecurityTab();
    void createAdvancedTab();

    void setupGeneralPageComponents();
    void setupConnectionPageComponents();
    void setupDisplayPageComponents();
    void setupAudioPageComponents();
    void setupSecurityPageComponents();
    void setupAdvancedPageComponents();

    void updateLanguageList();
    void updateThemeList();
    void updateAudioDeviceList();

    // UI组件
    class QListWidget* m_categoryListWidget;
    class QStackedWidget* m_settingsStackedWidget;

    // 常规设置选项卡
    class QWidget* m_generalTab;
    class QComboBox* m_languageCombo;
    class QComboBox* m_themeCombo;
    class QCheckBox* m_startWithSystemCheck;
    class QCheckBox* m_minimizeToTrayCheck;
    class QCheckBox* m_showNotificationsCheck;
    class QCheckBox* m_checkUpdatesCheck;

    // 连接设置选项卡
    class QWidget* m_connectionTab;
    class QSpinBox* m_defaultPortSpinBox;
    class QSpinBox* m_connectionTimeoutSpinBox;
    class QCheckBox* m_autoReconnectCheck;
    class QSpinBox* m_reconnectIntervalSpinBox;
    class QSpinBox* m_maxReconnectAttemptsSpinBox;
    class QCheckBox* m_enableUPnPCheck;
    class QLineEdit* m_proxyHostEdit;
    class QSpinBox* m_proxyPortSpinBox;
    class QLineEdit* m_proxyUsernameEdit;
    class QLineEdit* m_proxyPasswordEdit;

    // 显示设置选项卡
    class QWidget* m_displayTab;

    class QSpinBox* m_frameRateSpinBox;
    class QComboBox* m_colorDepthCombo;
    class QCheckBox* m_enableCursorCheck;
    class QCheckBox* m_enableWallpaperCheck;
    class QCheckBox* m_enableAnimationsCheck;
    class QCheckBox* m_enableFontSmoothingCheck;
    class QComboBox* m_captureQualityCombo;
    class QComboBox* m_scalingModeCombo;

    // 音频设置选项卡
    class QWidget* m_audioTab;
    class QCheckBox* m_enableAudioCheck;
    class QComboBox* m_audioQualityCombo;
    class QComboBox* m_audioDeviceCombo;
    class QSlider* m_audioVolumeSlider;
    class QLabel* m_audioVolumeLabel;
    class QCheckBox* m_enableMicrophoneCheck;
    class QComboBox* m_microphoneDeviceCombo;
    class QSlider* m_microphoneVolumeSlider;
    class QLabel* m_microphoneVolumeLabel;

    // 安全设置选项卡
    class QWidget* m_securityTab;
    class QCheckBox* m_enableEncryptionCheck;
    class QComboBox* m_encryptionMethodCombo;
    class QCheckBox* m_requirePasswordCheck;
    class QSpinBox* m_passwordLengthSpinBox;
    class QCheckBox* m_passwordComplexityCheck;
    class QSpinBox* m_sessionTimeoutSpinBox;
    class QCheckBox* m_logSecurityEventsCheck;
    class QLineEdit* m_trustedHostsEdit;

    // 高级设置选项卡
    class QWidget* m_advancedTab;
    class QComboBox* m_loggingLevelCombo;
    class QTextEdit* m_loggingRulesEdit;
    class QLineEdit* m_logFilePathEdit;
    class QPushButton* m_browseLogPathButton;
    class QSpinBox* m_maxLogFileSizeSpinBox;
    class QSpinBox* m_maxLogFilesSpinBox;

    class QSpinBox* m_performanceUpdateIntervalSpinBox;
    class QCheckBox* m_enableDebugModeCheck;
    class QTextEdit* m_customSettingsEdit;

    // 按钮
    class QDialogButtonBox* m_buttonBox;
    class QPushButton* m_okButton;
    class QPushButton* m_cancelButton;
    class QPushButton* m_applyButton;
    class QPushButton* m_resetButton;
    class QPushButton* m_defaultsButton;
    class QPushButton* m_importButton;
    class QPushButton* m_exportButton;

    // 设置存储
    class QSettings* m_settings;

    // UI对象
    Ui::SettingsDialog* ui;

    // 设置结构体
    struct GeneralSettings {
        QString language;
        QString theme;
        bool startWithSystem;
        bool minimizeToTray;
        bool showNotifications;
        bool checkUpdates;
    } m_generalSettings;

    struct ConnectionSettings {
        int defaultPort;
        int connectionTimeout;
        bool autoReconnect;
        int reconnectInterval;
        int maxReconnectAttempts;
        bool enableUPnP;
        QString proxyHost;
        int proxyPort;
        QString proxyUsername;
        QString proxyPassword;
    } m_connectionSettings;

    struct DisplaySettings {
        int frameRate;
        QString colorDepth;
        bool enableCursor;
        bool enableWallpaper;
        bool enableAnimations;
        bool enableFontSmoothing;
        double captureQuality;
        QString scalingMode;
    } m_displaySettings;

    struct AudioSettings {
        bool enableAudio;
        QString audioQuality;
        QString audioDevice;
        int audioVolume;
        bool enableMicrophone;
        QString microphoneDevice;
        int microphoneVolume;
    } m_audioSettings;

    struct SecuritySettings {
        bool enableEncryption;
        QString encryptionMethod;
        bool requirePassword;
        int passwordLength;
        bool passwordComplexity;
        int sessionTimeout;
        bool logSecurityEvents;
        QString trustedHosts;
    } m_securitySettings;

    struct AdvancedSettings {
        QString loggingLevel;
        QString loggingRules;
        QString logFilePath;
        int maxLogFileSize;
        int maxLogFiles;

        int performanceUpdateInterval;
        bool enableDebugMode;
        QString customSettings;
    } m_advancedSettings;

    // 状态
    bool m_settingsChanged;
};

#endif // SETTINGSDIALOG_H