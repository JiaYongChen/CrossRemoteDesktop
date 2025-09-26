#pragma once

#include "../../common/data/DataRecord.h"
#include <QtCore/QObject>
#include <QtCore/QDateTime>
#include <QtCore/QStringList>
#include <QtCore/QMutex>
#include <QtCore/QTimer>
#include <QtCore/QDir>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <memory>

Q_DECLARE_LOGGING_CATEGORY(lcStorageManager)

/**
 * @brief 存储统计信息结构
 */
struct StorageStats {
    qint64 totalStoredFrames = 0;        ///< 总存储帧数
    qint64 totalStorageBytes = 0;        ///< 总存储字节数
    qint64 keyFrameCount = 0;            ///< 关键帧数量
    qint64 deltaFrameCount = 0;          ///< 差分帧数量
    double averageFrameSize = 0.0;       ///< 平均帧大小
    QDateTime oldestFrameTime;           ///< 最旧帧时间
    QDateTime newestFrameTime;           ///< 最新帧时间
    double storageEfficiency = 0.0;      ///< 存储效率（压缩比）
};

/**
 * @brief 存储管理器类
 * 
 * 负责管理帧数据的存储、检索和清理，支持关键帧存储、
 * 性能监控数据收集和智能存储策略。
 */
class StorageManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 存储策略枚举
     */
    enum class StoragePolicy {
        None,           ///< 不存储
        KeyFramesOnly,  ///< 仅存储关键帧
        RecentFrames,   ///< 存储最近的帧
        FullSession,    ///< 存储完整会话
        Diagnostic      ///< 仅存储诊断数据
    };
    Q_ENUM(StoragePolicy)

    /**
     * @brief 存储配置结构
     */
    struct StorageConfig {
        StoragePolicy policy = StoragePolicy::KeyFramesOnly;
        int maxStorageMB = 500;              ///< 最大存储空间(MB)
        int keyFrameIntervalSec = 10;        ///< 关键帧间隔(秒)
        int recentFrameCount = 30;           ///< 最近帧数量
        int retentionDays = 7;               ///< 数据保留天数
        bool compressStorage = true;         ///< 是否压缩存储
        bool enableDiagnostics = true;       ///< 是否启用诊断数据收集
        QString storageBasePath;             ///< 存储基础路径
    };

    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit StorageManager(QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~StorageManager() override;

    /**
     * @brief 初始化存储管理器
     * @param config 存储配置
     * @return 是否初始化成功
     */
    bool initialize(const StorageConfig& config);

    /**
     * @brief 存储帧数据
     * @param record 数据记录
     * @param isKeyFrame 是否为关键帧
     * @return 是否存储成功
     */
    bool storeFrame(const DataRecord& record, bool isKeyFrame = false);

    /**
     * @brief 检索帧数据
     * @param frameId 帧ID
     * @param record 输出的数据记录
     * @return 是否检索成功
     */
    bool retrieveFrame(const QString& frameId, DataRecord& record);

    /**
     * @brief 获取指定时间范围内的帧ID列表
     * @param from 开始时间
     * @param to 结束时间
     * @return 帧ID列表
     */
    QStringList getStoredFrameIds(const QDateTime& from, const QDateTime& to);

    /**
     * @brief 获取存储统计信息
     * @return 存储统计数据
     */
    StorageStats getStorageStatistics();

    /**
     * @brief 清理过期数据
     */
    void cleanupExpiredData();

    /**
     * @brief 获取当前配置
     * @return 存储配置
     */
    StorageConfig getCurrentConfig() const;

    /**
     * @brief 更新配置
     * @param config 新的存储配置
     */
    void updateConfig(const StorageConfig& config);

    /**
     * @brief 收集性能数据
     * @param operation 操作名称
     * @param durationMs 持续时间(毫秒)
     * @param metadata 额外元数据
     */
    void collectPerformanceData(const QString& operation, qint64 durationMs, 
                               const QJsonObject& metadata = QJsonObject());

    /**
     * @brief 收集错误信息
     * @param error 错误描述
     * @param context 错误上下文
     * @param severity 严重程度
     */
    void collectErrorData(const QString& error, const QString& context, 
                         const QString& severity = "warning");

    /**
     * @brief 生成性能报告
     * @param from 开始时间
     * @param to 结束时间
     * @return JSON格式的性能报告
     */
    QJsonObject generatePerformanceReport(const QDateTime& from, const QDateTime& to);

    /**
     * @brief 生成错误报告
     * @param from 开始时间
     * @param to 结束时间
     * @return JSON格式的错误报告
     */
    QJsonObject generateErrorReport(const QDateTime& from, const QDateTime& to);

signals:
    /**
     * @brief 存储空间不足信号
     * @param usedMB 已使用空间(MB)
     * @param limitMB 限制空间(MB)
     */
    void storageSpaceLow(int usedMB, int limitMB);

    /**
     * @brief 数据清理完成信号
     * @param cleanedFrames 清理的帧数
     * @param freedMB 释放的空间(MB)
     */
    void dataCleanupCompleted(int cleanedFrames, int freedMB);

    /**
     * @brief 存储错误信号
     * @param error 错误描述
     */
    void storageError(const QString& error);

public slots:
    /**
     * @brief 执行定期清理
     */
    void performPeriodicCleanup();

    /**
     * @brief 强制清理存储空间
     */
    void forceCleanup();

private slots:
    /**
     * @brief 检查存储空间使用情况
     */
    void checkStorageUsage();

private:
    /**
     * @brief 初始化存储目录
     * @return 是否成功
     */
    bool initializeStorageDirectories();

    /**
     * @brief 生成帧文件路径
     * @param frameId 帧ID
     * @param isKeyFrame 是否为关键帧
     * @return 文件路径
     */
    QString generateFrameFilePath(const QString& frameId, bool isKeyFrame);

    /**
     * @brief 保存帧到文件
     * @param record 数据记录
     * @param filePath 文件路径
     * @return 是否成功
     */
    bool saveFrameToFile(const DataRecord& record, const QString& filePath);

    /**
     * @brief 从文件加载帧
     * @param filePath 文件路径
     * @param record 输出的数据记录
     * @return 是否成功
     */
    bool loadFrameFromFile(const QString& filePath, DataRecord& record);

    /**
     * @brief 计算目录大小
     * @param dirPath 目录路径
     * @return 目录大小(字节)
     */
    qint64 calculateDirectorySize(const QString& dirPath);

    /**
     * @brief 清理旧文件
     * @param directory 目录
     * @param maxAge 最大年龄(天)
     * @return 清理的文件数
     */
    int cleanupOldFiles(const QDir& directory, int maxAge);

    /**
     * @brief 检查是否应该存储帧
     * @param record 数据记录
     * @param isKeyFrame 是否为关键帧
     * @return 是否应该存储
     */
    bool shouldStoreFrame(const DataRecord& record, bool isKeyFrame);

    /**
     * @brief 保存诊断数据
     * @param type 数据类型
     * @param data JSON数据
     */
    void saveDiagnosticData(const QString& type, const QJsonObject& data);

private:
    StorageConfig m_config;                    ///< 存储配置
    mutable QMutex m_mutex;                    ///< 线程安全锁
    
    // 存储路径
    QString m_frameStoragePath;                ///< 帧存储路径
    QString m_keyFramePath;                    ///< 关键帧路径
    QString m_deltaFramePath;                  ///< 差分帧路径
    QString m_diagnosticPath;                  ///< 诊断数据路径
    
    // 统计信息
    StorageStats m_stats;                      ///< 存储统计
    QDateTime m_lastKeyFrameTime;              ///< 最后关键帧时间
    
    // 定时器
    QTimer* m_cleanupTimer;                    ///< 清理定时器
    QTimer* m_statsTimer;                      ///< 统计更新定时器
    
    // 常量
    static constexpr int CLEANUP_INTERVAL_MS = 300000;     ///< 清理间隔(5分钟)
    static constexpr int STATS_UPDATE_INTERVAL_MS = 60000; ///< 统计更新间隔(1分钟)
    static constexpr double STORAGE_WARNING_THRESHOLD = 0.8; ///< 存储空间警告阈值
    static constexpr int MAX_FRAME_FILE_SIZE_MB = 50;      ///< 最大帧文件大小
};