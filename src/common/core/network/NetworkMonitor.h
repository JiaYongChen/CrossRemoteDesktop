#ifndef NETWORKMONITOR_H
#define NETWORKMONITOR_H

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QLoggingCategory>
#include <QtCore/QElapsedTimer>
#include <QtCore/QQueue>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <memory>
#include <atomic>

Q_DECLARE_LOGGING_CATEGORY(lcNetworkMonitor)
;;

/**
 * @brief 网络状况监控器类
 *
 * 监控网络性能指标，包括：
 * 1. 带宽测量（上传/下载）
 * 2. 延迟测量（RTT）
 * 3. 丢包率检测
 * 4. 网络稳定性评估
 * 5. 网络质量等级评定
 */
class NetworkMonitor : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 网络质量等级枚举
     */
    enum NetworkQuality {
        Excellent,      ///< 优秀：高带宽、低延迟、稳定
        Good,           ///< 良好：中等带宽、正常延迟
        Fair,           ///< 一般：低带宽或高延迟
        Poor,           ///< 差：很低带宽或很高延迟
        Unstable        ///< 不稳定：网络波动大
    };

    /**
     * @brief 网络统计信息结构体
     */
    struct NetworkStats {
        double uploadBandwidth;         ///< 上传带宽 (Mbps)
        double downloadBandwidth;       ///< 下载带宽 (Mbps)
        double averageLatency;          ///< 平均延迟 (ms)
        double jitter;                  ///< 延迟抖动 (ms)
        double packetLossRate;          ///< 丢包率 (%)
        NetworkQuality quality;         ///< 网络质量等级
        double stabilityScore;          ///< 稳定性评分 (0.0-1.0)
        QDateTime lastUpdateTime;       ///< 最后更新时间

        /**
         * @brief 默认构造函数
         */
        NetworkStats()
            : uploadBandwidth(0.0)
            , downloadBandwidth(0.0)
            , averageLatency(0.0)
            , jitter(0.0)
            , packetLossRate(0.0)
            , quality(Poor)
            , stabilityScore(0.0) {
        }
    };

    /**
     * @brief 网络监控配置结构体
     */
    struct MonitorConfig {
        bool enableBandwidthTest;       ///< 启用带宽测试
        bool enableLatencyTest;         ///< 启用延迟测试
        bool enablePacketLossTest;      ///< 启用丢包测试
        int updateInterval;             ///< 更新间隔 (ms)
        int latencyTestInterval;        ///< 延迟测试间隔 (ms)
        int bandwidthTestInterval;      ///< 带宽测试间隔 (ms)
        int maxLatencyHistory;          ///< 最大延迟历史记录数
        int maxBandwidthHistory;        ///< 最大带宽历史记录数
        QString testServerUrl;          ///< 测试服务器URL
        int testDataSize;               ///< 测试数据大小 (bytes)
        int timeoutMs;                  ///< 超时时间 (ms)

        /**
         * @brief 默认构造函数
         */
        MonitorConfig()
            : enableBandwidthTest(true)
            , enableLatencyTest(true)
            , enablePacketLossTest(true)
            , updateInterval(5000)
            , latencyTestInterval(1000)
            , bandwidthTestInterval(10000)
            , maxLatencyHistory(100)
            , maxBandwidthHistory(50)
            , testServerUrl("https://httpbin.org")
            , testDataSize(1024)
            , timeoutMs(5000) {
        }
    };

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit NetworkMonitor(QObject* parent = nullptr);

    /**
     * @brief 析构函数
     */
    ~NetworkMonitor() override;

    /**
     * @brief 获取单例实例
     * @return NetworkMonitor实例指针
     */
    static NetworkMonitor* instance();

    /**
     * @brief 初始化网络监控器
     * @param config 监控配置
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize(const MonitorConfig& config = MonitorConfig());

    /**
     * @brief 清理资源
     */
    void cleanup();

    /**
     * @brief 开始监控
     * @return true 启动成功，false 启动失败
     */
    bool startMonitoring();

    /**
     * @brief 停止监控
     */
    void stopMonitoring();

    /**
     * @brief 获取当前网络统计信息
     * @return 网络统计信息
     */
    NetworkStats getNetworkStats() const;

    /**
     * @brief 获取网络质量等级
     * @return 网络质量等级
     */
    NetworkQuality getNetworkQuality() const;

    /**
     * @brief 获取监控配置
     * @return 监控配置
     */
    MonitorConfig getMonitorConfig() const;

    /**
     * @brief 更新监控配置
     * @param config 新的监控配置
     */
    void updateMonitorConfig(const MonitorConfig& config);

    /**
     * @brief 重置统计信息
     */
    void resetStats();

    /**
     * @brief 检查网络是否可用
     * @return true 网络可用，false 网络不可用
     */
    bool isNetworkAvailable() const;

    /**
     * @brief 获取推荐的帧率
     * @return 推荐的帧率 (fps)
     */
    int getRecommendedFrameRate() const;

public slots:
    /**
     * @brief 手动触发网络测试
     */
    void triggerNetworkTest();

    /**
     * @brief 手动触发延迟测试
     */
    void triggerLatencyTest();

    /**
     * @brief 手动触发带宽测试
     */
    void triggerBandwidthTest();

signals:
    /**
     * @brief 网络统计信息更新信号
     * @param stats 网络统计信息
     */
    void networkStatsUpdated(const NetworkStats& stats);

    /**
     * @brief 网络质量变化信号
     * @param quality 新的网络质量等级
     */
    void networkQualityChanged(NetworkQuality quality);

    /**
     * @brief 网络状态变化信号
     * @param available 网络是否可用
     */
    void networkAvailabilityChanged(bool available);

    /**
     * @brief 网络监控错误信号
     * @param error 错误信息
     */
    void monitoringError(const QString& error);

private slots:
    /**
     * @brief 执行定期网络测试
     */
    void performNetworkTest();

    /**
     * @brief 执行延迟测试
     */
    void performLatencyTest();

    /**
     * @brief 执行带宽测试
     */
    void performBandwidthTest();

    /**
     * @brief 处理延迟测试响应
     */
    void handleLatencyTestResponse();

    /**
     * @brief 处理带宽测试响应
     */
    void handleBandwidthTestResponse();

    /**
     * @brief 处理网络错误
     * @param error 网络错误
     */
    void handleNetworkError(QNetworkReply::NetworkError error);

private:
    /**
     * @brief 计算网络质量等级
     * @return 网络质量等级
     */
    NetworkQuality calculateNetworkQuality() const;

    /**
     * @brief 计算稳定性评分
     * @return 稳定性评分 (0.0-1.0)
     */
    double calculateStabilityScore() const;

    /**
     * @brief 更新延迟统计
     * @param latency 延迟值 (ms)
     */
    void updateLatencyStats(double latency);

    /**
     * @brief 更新带宽统计
     * @param bandwidth 带宽值 (Mbps)
     * @param isUpload 是否为上传带宽
     */
    void updateBandwidthStats(double bandwidth, bool isUpload);

    /**
     * @brief 计算延迟抖动
     * @return 延迟抖动 (ms)
     */
    double calculateJitter() const;

    /**
     * @brief 检测丢包率
     * @return 丢包率 (%)
     */
    double detectPacketLoss() const;

    /**
     * @brief 创建测试数据
     * @param size 数据大小
     * @return 测试数据
     */
    QByteArray createTestData(int size) const;

private:
    static NetworkMonitor* s_instance;      ///< 单例实例
    static QMutex s_instanceMutex;          ///< 单例互斥锁

    mutable QMutex m_statsMutex;            ///< 统计信息互斥锁
    mutable QMutex m_configMutex;           ///< 配置互斥锁

    MonitorConfig m_config;                 ///< 监控配置
    NetworkStats m_stats;                   ///< 网络统计信息

    QNetworkAccessManager* m_networkManager; ///< 网络访问管理器
    QTimer* m_updateTimer;                  ///< 更新定时器
    QTimer* m_latencyTimer;                 ///< 延迟测试定时器
    QTimer* m_bandwidthTimer;               ///< 带宽测试定时器

    QQueue<double> m_latencyHistory;        ///< 延迟历史记录
    QQueue<double> m_uploadBandwidthHistory; ///< 上传带宽历史记录
    QQueue<double> m_downloadBandwidthHistory; ///< 下载带宽历史记录

    QElapsedTimer m_latencyTestTimer;       ///< 延迟测试计时器
    QElapsedTimer m_bandwidthTestTimer;     ///< 带宽测试计时器

    std::atomic<bool> m_isMonitoring;       ///< 是否正在监控
    std::atomic<bool> m_isInitialized;      ///< 是否已初始化
    std::atomic<int> m_testCounter;         ///< 测试计数器
    std::atomic<int> m_successfulTests;     ///< 成功测试计数

    // 常量定义
    static constexpr double EXCELLENT_BANDWIDTH_THRESHOLD = 50.0;   ///< 优秀带宽阈值 (Mbps)
    static constexpr double GOOD_BANDWIDTH_THRESHOLD = 20.0;        ///< 良好带宽阈值 (Mbps)
    static constexpr double FAIR_BANDWIDTH_THRESHOLD = 5.0;         ///< 一般带宽阈值 (Mbps)
    static constexpr double EXCELLENT_LATENCY_THRESHOLD = 20.0;     ///< 优秀延迟阈值 (ms)
    static constexpr double GOOD_LATENCY_THRESHOLD = 50.0;          ///< 良好延迟阈值 (ms)
    static constexpr double FAIR_LATENCY_THRESHOLD = 100.0;         ///< 一般延迟阈值 (ms)
    static constexpr double MAX_ACCEPTABLE_PACKET_LOSS = 1.0;       ///< 最大可接受丢包率 (%)
    static constexpr double STABILITY_THRESHOLD = 0.8;             ///< 稳定性阈值
};

#endif // NETWORKMONITOR_H