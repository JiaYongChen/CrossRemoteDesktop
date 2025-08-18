#ifndef SCREENCAPTURE_H
#define SCREENCAPTURE_H

#include <QtCore/QObject>
#include <QtGui/QImage>

class QTimer;



class ScreenCapture : public QObject
{
    Q_OBJECT
    
public:
    explicit ScreenCapture(QObject *parent = nullptr);
    ~ScreenCapture();
    
    void startCapture();
    void stopCapture();
    bool isCapturing() const;
    
    // FPS控制
    void setFrameRate(int fps);
    int frameRate() const;
    
    // 捕获质量控制
    void setCaptureQuality(double quality);
    double captureQuality() const;
    
    // 高清捕获控制
    void setHighDefinitionMode(bool enabled);
    bool isHighDefinitionMode() const;
    
    // 抗锯齿控制
    void setAntiAliasing(bool enabled);
    bool isAntiAliasing() const;
    
    // 缩放质量控制
    void setScaleQuality(bool highQuality);
    bool isHighScaleQuality() const;
    
    // 性能优化集成

    
signals:
    void frameReady(const QImage &frame);
    
public slots:
    void captureFrame();
    
private slots:
    
private:
    bool m_isCapturing;
    QTimer *m_captureTimer;
    int m_frameRate;
    double m_captureQuality;
    
    // 高清捕获相关
    bool m_highDefinitionMode;
    bool m_antiAliasing;
    bool m_highScaleQuality;
    
    // 性能优化相关

    
    // 私有辅助方法
    QImage applyAntiAliasing(const QImage &image);
    QImage enhanceImageQuality(const QImage &image);
};

#endif // SCREENCAPTURE_H