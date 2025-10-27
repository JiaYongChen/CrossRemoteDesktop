#include "NetworkMonitor.h"
#include <QtCore/QDebug>
#include <QtCore/QMutexLocker>
#include <QtCore/QRandomGenerator>
#include <QtCore/QCoreApplication>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <cmath>
#include <algorithm>

Q_LOGGING_CATEGORY(lcNetworkMonitor, "networkmonitor", QtDebugMsg)

// 静态成员初始化
NetworkMonitor* NetworkMonitor::s_instance = nullptr;
QMutex NetworkMonitor::s_instanceMutex;

NetworkMonitor::NetworkMonitor(QObject* parent)
    : QObject(parent)
    , m_statsMutex()
    , m_configMutex()
    , m_networkManager(nullptr)
    , m_updateTimer(nullptr)
    , m_latencyTimer(nullptr)
    , m_bandwidthTimer(nullptr)
    , m_isMonitoring(false)
    , m_isInitialized(false)
    , m_testCounter(0)
    , m_successfulTests(0) {
    qCInfo(lcNetworkMonitor, "NetworkMonitor created");
}

NetworkMonitor::~NetworkMonitor() {
    cleanup();
    qCInfo(lcNetworkMonitor, "NetworkMonitor destroyed");
}

NetworkMonitor* NetworkMonitor::instance() {
    QMutexLocker locker(&s_instanceMutex);
    if ( !s_instance ) {
        s_instance = new NetworkMonitor();
    }
    return s_instance;
}

bool NetworkMonitor::initialize(const MonitorConfig& config) {
    QMutexLocker configLocker(&m_configMutex);

    if ( m_isInitialized.load() ) {
        qCWarning(lcNetworkMonitor, "NetworkMonitor already initialized");
        return true;
    }

    // 保存配置
    m_config = config;

    // 创建网络访问管理器
    m_networkManager = new QNetworkAccessManager(this);

    // 创建定时器
    m_updateTimer = new QTimer(this);
    m_latencyTimer = new QTimer(this);
    m_bandwidthTimer = new QTimer(this);

    // 连接信号槽
    connect(m_updateTimer, &QTimer::timeout, this, &NetworkMonitor::performNetworkTest);
    connect(m_latencyTimer, &QTimer::timeout, this, &NetworkMonitor::performLatencyTest);
    connect(m_bandwidthTimer, &QTimer::timeout, this, &NetworkMonitor::performBandwidthTest);

    // 初始化统计信息
    resetStats();

    m_isInitialized.store(true);
    qCInfo(lcNetworkMonitor, "NetworkMonitor initialized successfully");
    return true;
}

void NetworkMonitor::cleanup() {
    if ( !m_isInitialized.load() ) {
        return;
    }

    // 停止监控
    stopMonitoring();

    // 清理定时器
    if ( m_updateTimer ) {
        m_updateTimer->stop();
        m_updateTimer->deleteLater();
        m_updateTimer = nullptr;
    }

    if ( m_latencyTimer ) {
        m_latencyTimer->stop();
        m_latencyTimer->deleteLater();
        m_latencyTimer = nullptr;
    }

    if ( m_bandwidthTimer ) {
        m_bandwidthTimer->stop();
        m_bandwidthTimer->deleteLater();
        m_bandwidthTimer = nullptr;
    }

    // 清理网络管理器
    if ( m_networkManager ) {
        m_networkManager->deleteLater();
        m_networkManager = nullptr;
    }

    m_isInitialized.store(false);
    qCInfo(lcNetworkMonitor, "NetworkMonitor cleaned up");
}

bool NetworkMonitor::startMonitoring() {
    if ( !m_isInitialized.load() ) {
        qCWarning(lcNetworkMonitor, "NetworkMonitor not initialized");
        return false;
    }

    if ( m_isMonitoring.load() ) {
        qCWarning(lcNetworkMonitor, "NetworkMonitor already monitoring");
        return true;
    }

    QMutexLocker configLocker(&m_configMutex);

    // 启动定时器
    if ( m_config.enableLatencyTest && m_latencyTimer ) {
        m_latencyTimer->start(m_config.latencyTestInterval);
    }

    if ( m_config.enableBandwidthTest && m_bandwidthTimer ) {
        m_bandwidthTimer->start(m_config.bandwidthTestInterval);
    }

    if ( m_updateTimer ) {
        m_updateTimer->start(m_config.updateInterval);
    }

    m_isMonitoring.store(true);
    qCInfo(lcNetworkMonitor, "NetworkMonitor started monitoring");
    return true;
}

void NetworkMonitor::stopMonitoring() {
    if ( !m_isMonitoring.load() ) {
        return;
    }

    // 停止所有定时器
    if ( m_updateTimer && m_updateTimer->isActive() ) {
        m_updateTimer->stop();
    }

    if ( m_latencyTimer && m_latencyTimer->isActive() ) {
        m_latencyTimer->stop();
    }

    if ( m_bandwidthTimer && m_bandwidthTimer->isActive() ) {
        m_bandwidthTimer->stop();
    }

    m_isMonitoring.store(false);
    qCInfo(lcNetworkMonitor, "NetworkMonitor stopped monitoring");
}

NetworkMonitor::NetworkStats NetworkMonitor::getNetworkStats() const {
    QMutexLocker locker(&m_statsMutex);
    return m_stats;
}

NetworkMonitor::NetworkQuality NetworkMonitor::getNetworkQuality() const {
    QMutexLocker locker(&m_statsMutex);
    return m_stats.quality;
}

NetworkMonitor::MonitorConfig NetworkMonitor::getMonitorConfig() const {
    QMutexLocker locker(&m_configMutex);
    return m_config;
}

void NetworkMonitor::updateMonitorConfig(const MonitorConfig& config) {
    QMutexLocker locker(&m_configMutex);

    bool wasMonitoring = m_isMonitoring.load();
    if ( wasMonitoring ) {
        stopMonitoring();
    }

    m_config = config;

    if ( wasMonitoring ) {
        startMonitoring();
    }

    qCInfo(lcNetworkMonitor, "NetworkMonitor configuration updated");
}

void NetworkMonitor::resetStats() {
    QMutexLocker locker(&m_statsMutex);

    m_stats = NetworkStats();
    m_latencyHistory.clear();
    m_uploadBandwidthHistory.clear();
    m_downloadBandwidthHistory.clear();
    m_testCounter.store(0);
    m_successfulTests.store(0);

    qCInfo(lcNetworkMonitor, "NetworkMonitor statistics reset");
}

bool NetworkMonitor::isNetworkAvailable() const {
    if ( !m_networkManager ) {
        return false;
    }

    // 在 Qt6 中，networkAccessible() 已被移除
    // 我们简单地检查网络管理器是否存在，实际的网络可用性将通过测试来验证
    return true;
}

int NetworkMonitor::getRecommendedFrameRate() const {
    QMutexLocker locker(&m_statsMutex);

    switch ( m_stats.quality ) {
        case Excellent:
            return 60; // 高帧率
        case Good:
            return 30; // 标准帧率
        case Fair:
            return 20; // 中等帧率
        case Poor:
            return 15; // 低帧率
        case Unstable:
            return 10; // 最低帧率
        default:
            return 20; // 默认中等帧率
    }
}

void NetworkMonitor::triggerNetworkTest() {
    if ( !m_isInitialized.load() ) {
        qCWarning(lcNetworkMonitor, "NetworkMonitor not initialized");
        return;
    }

    performNetworkTest();
}

void NetworkMonitor::triggerLatencyTest() {
    if ( !m_isInitialized.load() ) {
        qCWarning(lcNetworkMonitor, "NetworkMonitor not initialized");
        return;
    }

    performLatencyTest();
}

void NetworkMonitor::triggerBandwidthTest() {
    if ( !m_isInitialized.load() ) {
        qCWarning(lcNetworkMonitor, "NetworkMonitor not initialized");
        return;
    }

    performBandwidthTest();
}

void NetworkMonitor::performNetworkTest() {
    if ( !isNetworkAvailable() ) {
        emit networkAvailabilityChanged(false);
        return;
    }

    // 更新网络质量
    QMutexLocker locker(&m_statsMutex);
    NetworkQuality oldQuality = m_stats.quality;
    m_stats.quality = calculateNetworkQuality();
    m_stats.stabilityScore = calculateStabilityScore();
    m_stats.lastUpdateTime = QDateTime::currentDateTime();

    if ( oldQuality != m_stats.quality ) {
        emit networkQualityChanged(m_stats.quality);
    }

    emit networkStatsUpdated(m_stats);
}

void NetworkMonitor::performLatencyTest() {
    if ( !m_networkManager || !isNetworkAvailable() ) {
        return;
    }

    QMutexLocker configLocker(&m_configMutex);
    QString testUrl = m_config.testServerUrl + "/get";

    QNetworkRequest request{ QUrl(testUrl) };
    request.setRawHeader("User-Agent", "QtRemoteDesktop-NetworkMonitor");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

    m_latencyTestTimer.start();
    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, &NetworkMonitor::handleLatencyTestResponse);
    connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
        this, &NetworkMonitor::handleNetworkError);

    // 设置超时
    QTimer::singleShot(m_config.timeoutMs, reply, &QNetworkReply::abort);
}

void NetworkMonitor::performBandwidthTest() {
    if ( !m_networkManager || !isNetworkAvailable() ) {
        return;
    }

    QMutexLocker configLocker(&m_configMutex);

    // 创建测试数据
    QByteArray testData = createTestData(m_config.testDataSize);
    QString testUrl = m_config.testServerUrl + "/post";

    QNetworkRequest request{ QUrl(testUrl) };
    request.setRawHeader("User-Agent", "QtRemoteDesktop-NetworkMonitor");
    request.setRawHeader("Content-Type", "application/octet-stream");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

    m_bandwidthTestTimer.start();
    QNetworkReply* reply = m_networkManager->post(request, testData);

    connect(reply, &QNetworkReply::finished, this, &NetworkMonitor::handleBandwidthTestResponse);
    connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
        this, &NetworkMonitor::handleNetworkError);

    // 设置超时
    QTimer::singleShot(m_config.timeoutMs, reply, &QNetworkReply::abort);
}

void NetworkMonitor::handleLatencyTestResponse() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if ( !reply ) {
        return;
    }

    qint64 latency = m_latencyTestTimer.elapsed();

    if ( reply->error() == QNetworkReply::NoError ) {
        updateLatencyStats(static_cast<double>(latency));
        m_successfulTests.fetch_add(1);
        qCDebug(lcNetworkMonitor, "Latency test completed: %lld ms", latency);
    } else {
        qCWarning(lcNetworkMonitor, "Latency test failed: %s", qPrintable(reply->errorString()));
    }

    m_testCounter.fetch_add(1);
    reply->deleteLater();
}

void NetworkMonitor::handleBandwidthTestResponse() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if ( !reply ) {
        return;
    }

    qint64 elapsedMs = m_bandwidthTestTimer.elapsed();

    if ( reply->error() == QNetworkReply::NoError && elapsedMs > 0 ) {
        QMutexLocker configLocker(&m_configMutex);

        // 计算上传带宽 (Mbps)
        double uploadBandwidth = (m_config.testDataSize * 8.0) / (elapsedMs * 1000.0);
        updateBandwidthStats(uploadBandwidth, true);

        // 计算下载带宽 (基于响应大小)
        QByteArray responseData = reply->readAll();
        if ( !responseData.isEmpty() ) {
            double downloadBandwidth = (responseData.size() * 8.0) / (elapsedMs * 1000.0);
            updateBandwidthStats(downloadBandwidth, false);
        }

        m_successfulTests.fetch_add(1);
        qCDebug(lcNetworkMonitor, "Bandwidth test completed: upload=%.2f Mbps, time=%lld ms",
            uploadBandwidth, elapsedMs);
    } else {
        qCWarning(lcNetworkMonitor, "Bandwidth test failed: %s", qPrintable(reply->errorString()));
    }

    m_testCounter.fetch_add(1);
    reply->deleteLater();
}

void NetworkMonitor::handleNetworkError(QNetworkReply::NetworkError error) {
    QString errorString = QString("Network error: %1").arg(static_cast<int>(error));
    qCWarning(lcNetworkMonitor, "%s", qPrintable(errorString));
    emit monitoringError(errorString);
}

NetworkMonitor::NetworkQuality NetworkMonitor::calculateNetworkQuality() const {
    // 基于带宽和延迟计算网络质量
    double bandwidth = std::max(m_stats.uploadBandwidth, m_stats.downloadBandwidth);
    double latency = m_stats.averageLatency;
    double stability = m_stats.stabilityScore;

    // 检查是否不稳定
    if ( stability < STABILITY_THRESHOLD ) {
        return Unstable;
    }

    // 检查丢包率
    if ( m_stats.packetLossRate > MAX_ACCEPTABLE_PACKET_LOSS ) {
        return Poor;
    }

    // 基于带宽和延迟评级
    if ( bandwidth >= EXCELLENT_BANDWIDTH_THRESHOLD && latency <= EXCELLENT_LATENCY_THRESHOLD ) {
        return Excellent;
    } else if ( bandwidth >= GOOD_BANDWIDTH_THRESHOLD && latency <= GOOD_LATENCY_THRESHOLD ) {
        return Good;
    } else if ( bandwidth >= FAIR_BANDWIDTH_THRESHOLD && latency <= FAIR_LATENCY_THRESHOLD ) {
        return Fair;
    } else {
        return Poor;
    }
}

double NetworkMonitor::calculateStabilityScore() const {
    if ( m_latencyHistory.size() < 5 ) {
        return 0.5; // 数据不足，返回中等稳定性
    }

    // 计算延迟变异系数
    double sum = 0.0;
    double sumSquares = 0.0;
    int count = m_latencyHistory.size();

    for ( double latency : m_latencyHistory ) {
        sum += latency;
        sumSquares += latency * latency;
    }

    double mean = sum / count;
    double variance = (sumSquares / count) - (mean * mean);
    double stdDev = std::sqrt(variance);

    // 变异系数 = 标准差 / 平均值
    double coefficientOfVariation = (mean > 0) ? (stdDev / mean) : 1.0;

    // 稳定性评分 = 1 - 变异系数（限制在0-1范围内）
    return std::max(0.0, std::min(1.0, 1.0 - coefficientOfVariation));
}

void NetworkMonitor::updateLatencyStats(double latency) {
    QMutexLocker locker(&m_statsMutex);
    QMutexLocker configLocker(&m_configMutex);

    // 添加到历史记录
    m_latencyHistory.enqueue(latency);
    if ( m_latencyHistory.size() > m_config.maxLatencyHistory ) {
        m_latencyHistory.dequeue();
    }

    // 计算平均延迟
    double sum = 0.0;
    for ( double l : m_latencyHistory ) {
        sum += l;
    }
    m_stats.averageLatency = sum / m_latencyHistory.size();

    // 计算抖动
    m_stats.jitter = calculateJitter();
}

void NetworkMonitor::updateBandwidthStats(double bandwidth, bool isUpload) {
    QMutexLocker locker(&m_statsMutex);
    QMutexLocker configLocker(&m_configMutex);

    if ( isUpload ) {
        m_uploadBandwidthHistory.enqueue(bandwidth);
        if ( m_uploadBandwidthHistory.size() > m_config.maxBandwidthHistory ) {
            m_uploadBandwidthHistory.dequeue();
        }

        // 计算平均上传带宽
        double sum = 0.0;
        for ( double b : m_uploadBandwidthHistory ) {
            sum += b;
        }
        m_stats.uploadBandwidth = sum / m_uploadBandwidthHistory.size();
    } else {
        m_downloadBandwidthHistory.enqueue(bandwidth);
        if ( m_downloadBandwidthHistory.size() > m_config.maxBandwidthHistory ) {
            m_downloadBandwidthHistory.dequeue();
        }

        // 计算平均下载带宽
        double sum = 0.0;
        for ( double b : m_downloadBandwidthHistory ) {
            sum += b;
        }
        m_stats.downloadBandwidth = sum / m_downloadBandwidthHistory.size();
    }
}

double NetworkMonitor::calculateJitter() const {
    if ( m_latencyHistory.size() < 2 ) {
        return 0.0;
    }

    double sum = 0.0;
    auto it = m_latencyHistory.begin();
    double prev = *it;
    ++it;

    for ( ; it != m_latencyHistory.end(); ++it ) {
        sum += std::abs(*it - prev);
        prev = *it;
    }

    return sum / (m_latencyHistory.size() - 1);
}

double NetworkMonitor::detectPacketLoss() const {
    int totalTests = m_testCounter.load();
    int successfulTests = m_successfulTests.load();

    if ( totalTests == 0 ) {
        return 0.0;
    }

    return ((totalTests - successfulTests) * 100.0) / totalTests;
}

QByteArray NetworkMonitor::createTestData(int size) const {
    QByteArray data;
    data.reserve(size);

    // 生成随机测试数据
    for ( int i = 0; i < size; ++i ) {
        data.append(static_cast<char>(QRandomGenerator::global()->bounded(256)));
    }

    return data;
}