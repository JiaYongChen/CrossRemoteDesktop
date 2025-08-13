#include "screencapture.h"
#include "../common/core/uiconstants.h"

#include <QApplication>
#include <QScreen>
#include <QDebug>
#include <QPixmap>
#include <QPainter>
#include <QDebug>

ScreenCapture::ScreenCapture(QObject *parent)
    : QObject(parent)
    , m_isCapturing(false)
    , m_captureTimer(new QTimer(this))
    , m_frameRate(UIConstants::MAX_FRAME_RATE)
    , m_captureQuality(UIConstants::DEFAULT_CAPTURE_QUALITY)
    , m_highDefinitionMode(true)
    , m_antiAliasing(true)
    , m_highScaleQuality(true)

{
    connect(m_captureTimer, &QTimer::timeout, this, &ScreenCapture::captureFrame);
    
    // 确保定时器设置为重复模式
    m_captureTimer->setSingleShot(false);
    
    setFrameRate(m_frameRate); // 设置默认帧率
}

ScreenCapture::~ScreenCapture()
{
    stopCapture();
}

void ScreenCapture::startCapture()
{
    if (m_isCapturing) {
        qDebug() << "ScreenCapture: Already capturing, ignoring start request";
        return;
    }
    
    qDebug() << "ScreenCapture: Starting capture with interval:" << m_captureTimer->interval() << "ms";
    m_isCapturing = true;
    m_captureTimer->start();
    
    // 添加调试信息确认定时器状态
    qDebug() << "ScreenCapture: Timer active:" << m_captureTimer->isActive();
    qDebug() << "ScreenCapture: Timer interval:" << m_captureTimer->interval();
    qDebug() << "ScreenCapture: Timer single shot:" << m_captureTimer->isSingleShot();
    

    
    // emit captureStarted();
    qDebug() << "ScreenCapture: Capture started successfully";
    
    // 手动触发一次捕获来测试
    QTimer::singleShot(100, this, &ScreenCapture::captureFrame);
}

void ScreenCapture::stopCapture()
{
    if (!m_isCapturing) {
        qDebug() << "ScreenCapture: Already stopped, ignoring stop request";
        return;
    }
    
    qDebug() << "ScreenCapture: Stopping capture";
    m_isCapturing = false;
    m_captureTimer->stop();
    qDebug() << "ScreenCapture: Capture stopped successfully";
}

bool ScreenCapture::isCapturing() const
{
    return m_isCapturing;
}

void ScreenCapture::captureFrame()
{
    try {
        // 添加调试信息确认函数被调用
        static int callCount = 0;
        callCount++;
        qDebug() << "ScreenCapture::captureFrame() called, count:" << callCount << "isCapturing:" << m_isCapturing;
        
        // 检查定时器状态
        qDebug() << "Timer active:" << m_captureTimer->isActive() << "interval:" << m_captureTimer->interval() << "singleShot:" << m_captureTimer->isSingleShot();
        
        if (!m_isCapturing) {
            qDebug() << "ScreenCapture::captureFrame() - Not capturing, returning";
            return;
        }
    
        // 获取主屏幕
        QScreen *screen = QApplication::primaryScreen();
        if (!screen) {
            static bool screenErrorLogged = false;
            if (!screenErrorLogged) {
                qDebug() << "No primary screen found";
                screenErrorLogged = true;
            }
            return;
        }
        
        qDebug() << "Screen found:" << screen->name() << "geometry:" << screen->geometry();
        
        // 高质量捕获屏幕
        QPixmap screenshot;
        qDebug() << "Starting screen capture with quality:" << m_captureQuality << "HD mode:" << m_highDefinitionMode;
        
        // 获取屏幕的设备像素比例
        qreal devicePixelRatio = screen->devicePixelRatio();
        QRect screenGeometry = screen->geometry();
        
        if (m_highDefinitionMode) {
            // 高清模式：使用原生分辨率捕获
            qDebug() << "High definition capture - devicePixelRatio:" << devicePixelRatio;
            
            // 计算实际像素尺寸
            QSize actualSize = screenGeometry.size() * devicePixelRatio;
            qDebug() << "Actual capture size:" << actualSize << "from geometry:" << screenGeometry.size();
            
            // 使用grabWindow捕获整个屏幕
            screenshot = screen->grabWindow(0, 0, 0, screenGeometry.width(), screenGeometry.height());
            
            // 确保保持设备像素比例
            screenshot.setDevicePixelRatio(devicePixelRatio);
            
            // 如果启用了抗锯齿，进行后处理
            if (m_antiAliasing && !screenshot.isNull()) {
                screenshot = applyAntiAliasing(screenshot);
            }
        } else {
            // 标准模式：简单捕获
            screenshot = screen->grabWindow(0);
            
            // 根据质量设置决定是否保持设备像素比例
            if (m_captureQuality >= 0.9) {
                screenshot.setDevicePixelRatio(devicePixelRatio);
            }
        }
        
        qDebug() << "Screenshot captured, isNull:" << screenshot.isNull() << "size:" << screenshot.size();
        
        if (!screenshot.isNull()) {
            // 应用图像质量增强
            if (m_highScaleQuality) {
                screenshot = enhanceImageQuality(screenshot);
            }
            
            // 输出每次捕获的调试信息
            static int captureCount = 0;
            captureCount++;
            qDebug() << "ScreenCapture: Frame captured (count:" << captureCount << "), size:" << screenshot.size() 
                     << ", quality:" << m_captureQuality << ", HD:" << m_highDefinitionMode 
                     << ", AA:" << m_antiAliasing << ", HQ:" << m_highScaleQuality;
            emit frameReady(screenshot);
        } else {
            static int failureCount = 0;
            failureCount++;
            qDebug() << "ScreenCapture: Failed to capture frame - screenshot is null (failures:" << failureCount << ")";
            
            // 如果连续失败，可能需要停止定时器
            if (failureCount > 5) {
                qDebug() << "ScreenCapture: Too many failures, stopping capture";
                stopCapture();
            }
        }
    } catch (const std::exception& e) {
        qDebug() << "ScreenCapture::captureFrame() - Exception caught:" << e.what();
        qDebug() << "Timer status after exception - active:" << m_captureTimer->isActive() << "interval:" << m_captureTimer->interval();
    } catch (...) {
        qDebug() << "ScreenCapture::captureFrame() - Unknown exception caught";
        qDebug() << "Timer status after unknown exception - active:" << m_captureTimer->isActive() << "interval:" << m_captureTimer->interval();
    }
    
    // 添加函数结束时的调试信息
    qDebug() << "ScreenCapture::captureFrame() completed - Timer active:" << m_captureTimer->isActive() << "isCapturing:" << m_isCapturing;
}

void ScreenCapture::setFrameRate(int fps)
{
    // 限制FPS范围在1-120之间
    m_frameRate = qBound(UIConstants::MIN_FRAME_RATE, fps, UIConstants::MAX_FRAME_RATE);
    
    // 计算定时器间隔（毫秒）
    int interval = UIConstants::MILLISECONDS_PER_SECOND / m_frameRate;
    m_captureTimer->setInterval(interval);
    
    qDebug() << "ScreenCapture: Frame rate set to" << m_frameRate << "FPS, interval:" << interval << "ms";
}

int ScreenCapture::frameRate() const
{
    return m_frameRate;
}

void ScreenCapture::setCaptureQuality(double quality)
{
    // 限制质量范围在0.1-1.0之间
    m_captureQuality = qBound(0.1, quality, 1.0);
    qDebug() << "ScreenCapture: Capture quality set to" << m_captureQuality;
}

double ScreenCapture::captureQuality() const
{
    return m_captureQuality;
}

// 高清捕获控制方法实现
void ScreenCapture::setHighDefinitionMode(bool enabled)
{
    m_highDefinitionMode = enabled;
    qDebug() << "ScreenCapture: High definition mode set to" << enabled;
}

bool ScreenCapture::isHighDefinitionMode() const
{
    return m_highDefinitionMode;
}

void ScreenCapture::setAntiAliasing(bool enabled)
{
    m_antiAliasing = enabled;
    qDebug() << "ScreenCapture: Anti-aliasing set to" << enabled;
}

bool ScreenCapture::isAntiAliasing() const
{
    return m_antiAliasing;
}

void ScreenCapture::setScaleQuality(bool highQuality)
{
    m_highScaleQuality = highQuality;
    qDebug() << "ScreenCapture: Scale quality set to" << (highQuality ? "high" : "normal");
}

bool ScreenCapture::isHighScaleQuality() const
{
    return m_highScaleQuality;
}

// 私有辅助方法实现
QPixmap ScreenCapture::applyAntiAliasing(const QPixmap &pixmap)
{
    if (pixmap.isNull()) {
        return pixmap;
    }
    
    // 创建一个新的QPixmap用于抗锯齿处理
    QPixmap smoothPixmap(pixmap.size());
    smoothPixmap.fill(Qt::transparent);
    
    QPainter painter(&smoothPixmap);
    
    // 启用高质量渲染
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    
    // 绘制原始图像
    painter.drawPixmap(0, 0, pixmap);
    painter.end();
    
    // 保持设备像素比例
    smoothPixmap.setDevicePixelRatio(pixmap.devicePixelRatio());
    
    qDebug() << "Applied anti-aliasing to pixmap, size:" << smoothPixmap.size();
    return smoothPixmap;
}

QPixmap ScreenCapture::enhanceImageQuality(const QPixmap &pixmap)
{
    if (pixmap.isNull()) {
        return pixmap;
    }
    
    // 如果不需要高质量缩放，直接返回
    if (!m_highScaleQuality) {
        return pixmap;
    }
    
    // 创建增强质量的图像
    QPixmap enhancedPixmap(pixmap.size());
    enhancedPixmap.fill(Qt::transparent);
    
    QPainter painter(&enhancedPixmap);
    
    // 设置最高质量的渲染选项
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    
    // 使用高质量的合成模式
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    
    // 绘制图像
    painter.drawPixmap(0, 0, pixmap);
    painter.end();
    
    // 保持设备像素比例
    enhancedPixmap.setDevicePixelRatio(pixmap.devicePixelRatio());
    
    qDebug() << "Enhanced image quality for pixmap, size:" << enhancedPixmap.size();
    return enhancedPixmap;
}

// 性能优化方法实现