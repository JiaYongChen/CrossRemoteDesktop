#pragma once

#include <QtCore/QObject>
#include <QtCore/QSettings>
#include <QtCore/QString>

/**
 * @brief 数据处理配置类
 * 
 * 管理数据处理模块的各种配置选项，支持从配置文件加载和保存设置。
 * 提供运行时配置控制，支持动态启用/禁用各种数据处理功能。
 */
class DataProcessingConfig : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit DataProcessingConfig(QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~DataProcessingConfig() override = default;

    // 配置属性访问器
    bool isValidationEnabled() const { return m_validationEnabled; }
    void setValidationEnabled(bool enabled);

    bool isCleaningEnabled() const { return m_cleaningEnabled; }
    void setCleaningEnabled(bool enabled);

    bool isStorageEnabled() const { return m_storageEnabled; }
    void setStorageEnabled(bool enabled);

    int storageLimitMB() const { return m_storageLimitMB; }
    void setStorageLimitMB(int limitMB);

    int keyFrameIntervalSec() const { return m_keyFrameIntervalSec; }
    void setKeyFrameIntervalSec(int intervalSec);

    bool isDebugMode() const { return m_debugMode; }
    void setDebugMode(bool enabled);

    // 配置文件操作
    void loadFromSettings();
    void saveToSettings();

    // 重置为默认值
    void resetToDefaults();

    // 验证配置有效性
    bool isValid() const;

signals:
    /**
     * @brief 配置变更信号
     * @param configName 配置项名称
     * @param newValue 新值
     */
    void configChanged(const QString& configName, const QVariant& newValue);

private:
    // 配置项
    bool m_validationEnabled{true};      ///< 启用数据验证
    bool m_cleaningEnabled{false};       ///< 启用数据清洗
    bool m_storageEnabled{false};        ///< 启用存储功能
    int m_storageLimitMB{100};          ///< 存储限制（MB）
    int m_keyFrameIntervalSec{5};       ///< 关键帧间隔（秒）
    bool m_debugMode{false};            ///< 调试模式

    // 配置文件相关
    static constexpr const char* CONFIG_GROUP = "DataProcessing";
    static constexpr const char* KEY_VALIDATION_ENABLED = "ValidationEnabled";
    static constexpr const char* KEY_CLEANING_ENABLED = "CleaningEnabled";
    static constexpr const char* KEY_STORAGE_ENABLED = "StorageEnabled";
    static constexpr const char* KEY_STORAGE_LIMIT = "StorageLimit";
    static constexpr const char* KEY_KEYFRAME_INTERVAL = "KeyFrameInterval";
    static constexpr const char* KEY_DEBUG_MODE = "DebugMode";

    // 默认值
    static constexpr bool DEFAULT_VALIDATION_ENABLED = true;
    static constexpr bool DEFAULT_CLEANING_ENABLED = false;
    static constexpr bool DEFAULT_STORAGE_ENABLED = false;
    static constexpr int DEFAULT_STORAGE_LIMIT_MB = 100;
    static constexpr int DEFAULT_KEYFRAME_INTERVAL_SEC = 5;
    static constexpr bool DEFAULT_DEBUG_MODE = false;

    // 限制值
    static constexpr int MIN_STORAGE_LIMIT_MB = 10;
    static constexpr int MAX_STORAGE_LIMIT_MB = 1000;
    static constexpr int MIN_KEYFRAME_INTERVAL_SEC = 1;
    static constexpr int MAX_KEYFRAME_INTERVAL_SEC = 60;
};