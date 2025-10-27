#include "DataProcessingConfig.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <QtCore/QLoggingCategory>
#include <QtCore/QStandardPaths>
#include <QtCore/QCoreApplication>

Q_LOGGING_CATEGORY(lcDataProcessingConfig, "server.dataprocessing.config")

DataProcessingConfig::DataProcessingConfig(QObject* parent)
    : QObject(parent) {
    qCDebug(lcDataProcessingConfig) << "DataProcessingConfig 构造函数调用";

    // 加载配置
    loadFromSettings();
}

void DataProcessingConfig::setValidationEnabled(bool enabled) {
    if ( m_validationEnabled != enabled ) {
        m_validationEnabled = enabled;
        qCDebug(lcDataProcessingConfig) << "数据验证已" << (enabled ? "启用" : "禁用");
        emit configChanged(KEY_VALIDATION_ENABLED, enabled);
    }
}

void DataProcessingConfig::setCleaningEnabled(bool enabled) {
    if ( m_cleaningEnabled != enabled ) {
        m_cleaningEnabled = enabled;
        qCDebug(lcDataProcessingConfig) << "数据清洗已" << (enabled ? "启用" : "禁用");
        emit configChanged(KEY_CLEANING_ENABLED, enabled);
    }
}

void DataProcessingConfig::setStorageEnabled(bool enabled) {
    if ( m_storageEnabled != enabled ) {
        m_storageEnabled = enabled;
        qCDebug(lcDataProcessingConfig) << "数据存储已" << (enabled ? "启用" : "禁用");
        emit configChanged(KEY_STORAGE_ENABLED, enabled);
    }
}

void DataProcessingConfig::setStorageLimitMB(int limitMB) {
    // 限制范围
    int clampedLimit = qBound(MIN_STORAGE_LIMIT_MB, limitMB, MAX_STORAGE_LIMIT_MB);

    if ( m_storageLimitMB != clampedLimit ) {
        m_storageLimitMB = clampedLimit;
        qCDebug(lcDataProcessingConfig) << "存储限制设置为" << clampedLimit << "MB";
        emit configChanged(KEY_STORAGE_LIMIT, clampedLimit);
    }

    if ( limitMB != clampedLimit ) {
        qCWarning(lcDataProcessingConfig) << "存储限制值" << limitMB
            << "超出范围，已调整为" << clampedLimit;
    }
}

void DataProcessingConfig::setKeyFrameIntervalSec(int intervalSec) {
    // 限制范围
    int clampedInterval = qBound(MIN_KEYFRAME_INTERVAL_SEC, intervalSec, MAX_KEYFRAME_INTERVAL_SEC);

    if ( m_keyFrameIntervalSec != clampedInterval ) {
        m_keyFrameIntervalSec = clampedInterval;
        qCDebug(lcDataProcessingConfig) << "关键帧间隔设置为" << clampedInterval << "秒";
        emit configChanged(KEY_KEYFRAME_INTERVAL, clampedInterval);
    }

    if ( intervalSec != clampedInterval ) {
        qCWarning(lcDataProcessingConfig) << "关键帧间隔值" << intervalSec
            << "超出范围，已调整为" << clampedInterval;
    }
}

void DataProcessingConfig::setDebugMode(bool enabled) {
    if ( m_debugMode != enabled ) {
        m_debugMode = enabled;
        qCDebug(lcDataProcessingConfig) << "调试模式已" << (enabled ? "启用" : "禁用");
        emit configChanged(KEY_DEBUG_MODE, enabled);
    }
}

void DataProcessingConfig::loadFromSettings() {
    QSettings settings;
    settings.beginGroup(CONFIG_GROUP);

    m_validationEnabled = settings.value(KEY_VALIDATION_ENABLED, DEFAULT_VALIDATION_ENABLED).toBool();
    m_cleaningEnabled = settings.value(KEY_CLEANING_ENABLED, DEFAULT_CLEANING_ENABLED).toBool();
    m_storageEnabled = settings.value(KEY_STORAGE_ENABLED, DEFAULT_STORAGE_ENABLED).toBool();
    m_storageLimitMB = settings.value(KEY_STORAGE_LIMIT, DEFAULT_STORAGE_LIMIT_MB).toInt();
    m_keyFrameIntervalSec = settings.value(KEY_KEYFRAME_INTERVAL, DEFAULT_KEYFRAME_INTERVAL_SEC).toInt();
    m_debugMode = settings.value(KEY_DEBUG_MODE, DEFAULT_DEBUG_MODE).toBool();

    settings.endGroup();

    // 验证加载的值
    m_storageLimitMB = qBound(MIN_STORAGE_LIMIT_MB, m_storageLimitMB, MAX_STORAGE_LIMIT_MB);
    m_keyFrameIntervalSec = qBound(MIN_KEYFRAME_INTERVAL_SEC, m_keyFrameIntervalSec, MAX_KEYFRAME_INTERVAL_SEC);

    qCDebug(lcDataProcessingConfig) << "配置已从设置文件加载:";
    qCDebug(lcDataProcessingConfig) << "  验证启用:" << m_validationEnabled;
    qCDebug(lcDataProcessingConfig) << "  清洗启用:" << m_cleaningEnabled;
    qCDebug(lcDataProcessingConfig) << "  存储启用:" << m_storageEnabled;
    qCDebug(lcDataProcessingConfig) << "  存储限制:" << m_storageLimitMB << "MB";
    qCDebug(lcDataProcessingConfig) << "  关键帧间隔:" << m_keyFrameIntervalSec << "秒";
    qCDebug(lcDataProcessingConfig) << "  调试模式:" << m_debugMode;
}

void DataProcessingConfig::saveToSettings() {
    QSettings settings;
    settings.beginGroup(CONFIG_GROUP);

    settings.setValue(KEY_VALIDATION_ENABLED, m_validationEnabled);
    settings.setValue(KEY_CLEANING_ENABLED, m_cleaningEnabled);
    settings.setValue(KEY_STORAGE_ENABLED, m_storageEnabled);
    settings.setValue(KEY_STORAGE_LIMIT, m_storageLimitMB);
    settings.setValue(KEY_KEYFRAME_INTERVAL, m_keyFrameIntervalSec);
    settings.setValue(KEY_DEBUG_MODE, m_debugMode);

    settings.endGroup();
    settings.sync();

    qCDebug(lcDataProcessingConfig) << "配置已保存到设置文件";
}

void DataProcessingConfig::resetToDefaults() {
    qCDebug(lcDataProcessingConfig) << "重置配置为默认值";

    setValidationEnabled(DEFAULT_VALIDATION_ENABLED);
    setCleaningEnabled(DEFAULT_CLEANING_ENABLED);
    setStorageEnabled(DEFAULT_STORAGE_ENABLED);
    setStorageLimitMB(DEFAULT_STORAGE_LIMIT_MB);
    setKeyFrameIntervalSec(DEFAULT_KEYFRAME_INTERVAL_SEC);
    setDebugMode(DEFAULT_DEBUG_MODE);
}

bool DataProcessingConfig::isValid() const {
    // 检查配置的有效性
    bool valid = true;

    if ( m_storageLimitMB < MIN_STORAGE_LIMIT_MB || m_storageLimitMB > MAX_STORAGE_LIMIT_MB ) {
        qCWarning(lcDataProcessingConfig) << "存储限制值无效:" << m_storageLimitMB;
        valid = false;
    }

    if ( m_keyFrameIntervalSec < MIN_KEYFRAME_INTERVAL_SEC || m_keyFrameIntervalSec > MAX_KEYFRAME_INTERVAL_SEC ) {
        qCWarning(lcDataProcessingConfig) << "关键帧间隔值无效:" << m_keyFrameIntervalSec;
        valid = false;
    }

    return valid;
}