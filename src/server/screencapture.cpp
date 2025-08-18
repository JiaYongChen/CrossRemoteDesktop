#include "screencapture.h"
#include "../common/core/constants.h"

#include <QtWidgets/QApplication>
#include <QtGui/QScreen>
#include <QtCore/QDebug>
#include <QtCore/QMessageLogger>
#include "../common/core/logging_categories.h"
#include <QtGui/QPixmap>
#include <QtGui/QPainter>
#include <QtCore/QDebug>
#include <QtCore/QTimer>

ScreenCapture::ScreenCapture(QObject *parent)
    : QObject(parent)
    , m_isCapturing(false)
    , m_captureTimer(new QTimer(this))
    , m_frameRate(CoreConstants::MAX_FRAME_RATE)
    , m_captureQuality(CoreConstants::DEFAULT_CAPTURE_QUALITY)
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
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "Already capturing, ignoring start request";
        return;
    }
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcCapture) << "Starting capture with interval:" << m_captureTimer->interval() << "ms";
    m_isCapturing = true;
    m_captureTimer->start();
    
    // 添加调试信息确认定时器状态
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "Timer active:" << m_captureTimer->isActive()
                       << "interval:" << m_captureTimer->interval()
                       << "singleShot:" << m_captureTimer->isSingleShot();
    

    
    // emit captureStarted();
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcCapture) << "Capture started successfully";
    
    // 手动触发一次捕获来测试
    QTimer::singleShot(100, this, &ScreenCapture::captureFrame);
}

void ScreenCapture::stopCapture()
{
    if (!m_isCapturing) {
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "Already stopped, ignoring stop request";
        return;
    }
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcCapture) << "Stopping capture";
    m_isCapturing = false;
    m_captureTimer->stop();
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcCapture) << "Capture stopped successfully";
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
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "ScreenCapture::captureFrame() called, count:" << callCount << "isCapturing:" << m_isCapturing;
        
        // 检查定时器状态
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "Timer active:" << m_captureTimer->isActive() << "interval:" << m_captureTimer->interval() << "singleShot:" << m_captureTimer->isSingleShot();
        
        if (!m_isCapturing) {
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "ScreenCapture::captureFrame() - Not capturing, returning";
            return;
        }
    
        // 获取主屏幕
        QScreen *screen = QApplication::primaryScreen();
        if (!screen) {
            static bool screenErrorLogged = false;
            if (!screenErrorLogged) {
                QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcCapture) << "No primary screen found";
                screenErrorLogged = true;
            }
            return;
        }
        
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "Screen found:" << screen->name() << "geometry:" << screen->geometry();
        
        // 高质量捕获屏幕
        QPixmap screenshot;
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "Starting screen capture with quality:" << m_captureQuality << "HD mode:" << m_highDefinitionMode;
        
        // 获取屏幕的设备像素比例
        qreal devicePixelRatio = screen->devicePixelRatio();
        QRect screenGeometry = screen->geometry();
        
        if (m_highDefinitionMode) {
            // 高清模式：使用原生分辨率捕获
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "High definition capture - devicePixelRatio:" << devicePixelRatio;
            
            // 计算实际像素尺寸
            QSize actualSize = screenGeometry.size() * devicePixelRatio;
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "Actual capture size:" << actualSize << "from geometry:" << screenGeometry.size();
            
            // 使用grabWindow捕获整个屏幕
            screenshot = screen->grabWindow(0, 0, 0, screenGeometry.width(), screenGeometry.height());
            
            // 确保保持设备像素比例
            screenshot.setDevicePixelRatio(devicePixelRatio);
            
            // 如果启用了抗锯齿，进行后处理
            if (m_antiAliasing && !screenshot.isNull()) {
                QImage tmp = applyAntiAliasing(screenshot.toImage());
                screenshot = QPixmap::fromImage(tmp);
            }
        } else {
            // 标准模式：简单捕获
            screenshot = screen->grabWindow(0);
            
            // 根据质量设置决定是否保持设备像素比例
            if (m_captureQuality >= 0.9) {
                screenshot.setDevicePixelRatio(devicePixelRatio);
            }
        }
        
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "Screenshot captured, isNull:" << screenshot.isNull() << "size:" << screenshot.size();
        
        if (!screenshot.isNull()) {
            // 应用图像质量增强
            if (m_highScaleQuality) {
                QImage tmp = enhanceImageQuality(screenshot.toImage());
                screenshot = QPixmap::fromImage(tmp);
            }
            
            // 输出每次捕获的调试信息
            static int captureCount = 0;
            captureCount++;
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "ScreenCapture: Frame captured (count:" << captureCount << "), size:" << screenshot.size() 
                     << ", quality:" << m_captureQuality << ", HD:" << m_highDefinitionMode 
                     << ", AA:" << m_antiAliasing << ", HQ:" << m_highScaleQuality;
            emit frameReady(screenshot.toImage());
        } else {
            static int failureCount = 0;
            failureCount++;
            QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcCapture) << "ScreenCapture: Failed to capture frame - screenshot is null (failures:" << failureCount << ")";
            
            // 如果连续失败，可能需要停止定时器
            if (failureCount > 5) {
                QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcCapture) << "ScreenCapture: Too many failures, stopping capture";
                stopCapture();
            }
        }
    } catch (const std::exception& e) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcCapture) << "ScreenCapture::captureFrame() - Exception caught:" << e.what();
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "Timer status after exception - active:" << m_captureTimer->isActive() << "interval:" << m_captureTimer->interval();
    } catch (...) {
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).warning(lcCapture) << "ScreenCapture::captureFrame() - Unknown exception caught";
        QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "Timer status after unknown exception - active:" << m_captureTimer->isActive() << "interval:" << m_captureTimer->interval();
    }
    
    // 添加函数结束时的调试信息
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "ScreenCapture::captureFrame() completed - Timer active:" << m_captureTimer->isActive() << "isCapturing:" << m_isCapturing;
}

void ScreenCapture::setFrameRate(int fps)
{
    // 限制FPS范围在1-120之间
    m_frameRate = qBound(CoreConstants::MIN_FRAME_RATE, fps, CoreConstants::MAX_FRAME_RATE);
    
    // 计算定时器间隔（毫秒）
    int interval = CoreConstants::MILLISECONDS_PER_SECOND / m_frameRate;
    m_captureTimer->setInterval(interval);
    
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcCapture) << "ScreenCapture: Frame rate set to" << m_frameRate << "FPS, interval:" << interval << "ms";
}

int ScreenCapture::frameRate() const
{
    return m_frameRate;
}

void ScreenCapture::setCaptureQuality(double quality)
{
    // 限制质量范围在0.1-1.0之间
    m_captureQuality = qBound(0.1, quality, 1.0);
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcCapture) << "ScreenCapture: Capture quality set to" << m_captureQuality;
}

double ScreenCapture::captureQuality() const
{
    return m_captureQuality;
}

// 高清捕获控制方法实现
void ScreenCapture::setHighDefinitionMode(bool enabled)
{
    m_highDefinitionMode = enabled;
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcCapture) << "ScreenCapture: High definition mode set to" << enabled;
}

bool ScreenCapture::isHighDefinitionMode() const
{
    return m_highDefinitionMode;
}

void ScreenCapture::setAntiAliasing(bool enabled)
{
    m_antiAliasing = enabled;
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcCapture) << "ScreenCapture: Anti-aliasing set to" << enabled;
}

bool ScreenCapture::isAntiAliasing() const
{
    return m_antiAliasing;
}

void ScreenCapture::setScaleQuality(bool highQuality)
{
    m_highScaleQuality = highQuality;
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info(lcCapture) << "ScreenCapture: Scale quality set to" << (highQuality ? "high" : "normal");
}

bool ScreenCapture::isHighScaleQuality() const
{
    return m_highScaleQuality;
}

// 私有辅助方法实现
QImage ScreenCapture::applyAntiAliasing(const QImage &image)
{
    if (image.isNull()) {
        return image;
    }
    QImage smoothImage(image.size(), QImage::Format_ARGB32_Premultiplied);
    smoothImage.fill(Qt::transparent);
    QPainter painter(&smoothImage);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.drawImage(QPoint(0, 0), image);
    painter.end();
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "Applied anti-aliasing to image, size:" << smoothImage.size();
    return smoothImage;
}

QImage ScreenCapture::enhanceImageQuality(const QImage &image)
{
    if (image.isNull()) {
        return image;
    }
    if (!m_highScaleQuality) {
        return image;
    }
    QImage enhanced(image.size(), QImage::Format_ARGB32_Premultiplied);
    enhanced.fill(Qt::transparent);
    QPainter painter(&enhanced);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(QPoint(0, 0), image);
    painter.end();
    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).debug(lcCapture) << "Enhanced image quality for image, size:" << enhanced.size();
    return enhanced;
}

// 性能优化方法实现