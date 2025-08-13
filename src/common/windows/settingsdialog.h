#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QtWidgets/QDialog>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QFontDialog>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtWidgets/QDialogButtonBox>

QT_BEGIN_NAMESPACE
namespace Ui { class SettingsDialog; }
QT_END_NAMESPACE

class SettingsDialog : public QDialog
{
    Q_OBJECT
    
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
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
    void onDefaultViewModeChanged(int index);
    void onCompressionLevelChanged(int value);
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
    void onLogFilePathChanged(const QString &text);
    void onPerformanceMonitoringChanged(bool checked);
    
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
    void showValidationError(const QString &message);
    
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
    QListWidget *m_categoryListWidget;
    QStackedWidget *m_settingsStackedWidget;
    
    // 常规设置选项卡
    QWidget *m_generalTab;
    QComboBox *m_languageCombo;
    QComboBox *m_themeCombo;
    QCheckBox *m_startWithSystemCheck;
    QCheckBox *m_minimizeToTrayCheck;
    QCheckBox *m_showNotificationsCheck;
    QCheckBox *m_checkUpdatesCheck;
    
    // 连接设置选项卡
    QWidget *m_connectionTab;
    QSpinBox *m_defaultPortSpinBox;
    QSpinBox *m_connectionTimeoutSpinBox;
    QCheckBox *m_autoReconnectCheck;
    QSpinBox *m_reconnectIntervalSpinBox;
    QSpinBox *m_maxReconnectAttemptsSpinBox;
    QCheckBox *m_enableUPnPCheck;
    QLineEdit *m_proxyHostEdit;
    QSpinBox *m_proxyPortSpinBox;
    QLineEdit *m_proxyUsernameEdit;
    QLineEdit *m_proxyPasswordEdit;
    
    // 显示设置选项卡
    QWidget *m_displayTab;
    QComboBox *m_defaultViewModeCombo;
    QSlider *m_compressionLevelSlider;
    QLabel *m_compressionLevelLabel;
    QSpinBox *m_frameRateSpinBox;
    QComboBox *m_colorDepthCombo;
    QCheckBox *m_enableCursorCheck;
    QCheckBox *m_enableWallpaperCheck;
    QCheckBox *m_enableAnimationsCheck;
    QCheckBox *m_enableFontSmoothingCheck;
    QComboBox *m_captureQualityCombo;
    QComboBox *m_scalingModeCombo;
    
    // 音频设置选项卡
    QWidget *m_audioTab;
    QCheckBox *m_enableAudioCheck;
    QComboBox *m_audioQualityCombo;
    QComboBox *m_audioDeviceCombo;
    QSlider *m_audioVolumeSlider;
    QLabel *m_audioVolumeLabel;
    QCheckBox *m_enableMicrophoneCheck;
    QComboBox *m_microphoneDeviceCombo;
    QSlider *m_microphoneVolumeSlider;
    QLabel *m_microphoneVolumeLabel;
    
    // 安全设置选项卡
    QWidget *m_securityTab;
    QCheckBox *m_enableEncryptionCheck;
    QComboBox *m_encryptionMethodCombo;
    QCheckBox *m_requirePasswordCheck;
    QSpinBox *m_passwordLengthSpinBox;
    QCheckBox *m_passwordComplexityCheck;
    QSpinBox *m_sessionTimeoutSpinBox;
    QCheckBox *m_logSecurityEventsCheck;
    QLineEdit *m_trustedHostsEdit;
    
    // 高级设置选项卡
    QWidget *m_advancedTab;
    QComboBox *m_loggingLevelCombo;
    QLineEdit *m_logFilePathEdit;
    QPushButton *m_browseLogPathButton;
    QSpinBox *m_maxLogFileSizeSpinBox;
    QSpinBox *m_maxLogFilesSpinBox;
    QCheckBox *m_enablePerformanceMonitoringCheck;
    QSpinBox *m_performanceUpdateIntervalSpinBox;
    QCheckBox *m_enableDebugModeCheck;
    QTextEdit *m_customSettingsEdit;
    
    // 按钮
    QDialogButtonBox *m_buttonBox;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    QPushButton *m_applyButton;
    QPushButton *m_resetButton;
    QPushButton *m_defaultsButton;
    QPushButton *m_importButton;
    QPushButton *m_exportButton;
    
    // 设置存储
    QSettings *m_settings;
    
    // UI对象
    Ui::SettingsDialog *ui;
    
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
        QString defaultViewMode;
        int compressionLevel;
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
        QString logFilePath;
        int maxLogFileSize;
        int maxLogFiles;
        bool enablePerformanceMonitoring;
        int performanceUpdateInterval;
        bool enableDebugMode;
        QString customSettings;
    } m_advancedSettings;
    
    // 状态
    bool m_settingsChanged;
};

#endif // SETTINGSDIALOG_H