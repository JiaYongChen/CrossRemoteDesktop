// AvFoundationCapture.h — macOS ScreenCaptureKit 屏幕捕获实现
#pragma once

#ifdef Q_OS_MACOS

#include "../IScreenCapture.h"
#include <QImage>
#include <QSize>
#include <QObject>
#include <memory>

/**
 * @brief macOS 屏幕捕获（ScreenCaptureKit / AVFoundation）
 *
 * 使用 PIMPL 模式隔离 ObjC 类型，头文件可被纯 C++ 编译。
 * 继承 QObject 以支持 screenCapturePermissionRequired 信号。
 *
 * 系统要求：macOS 13.0+（ScreenCaptureKit）
 * 回退策略：isAvailable() 返回 false → 工厂降级为 Qt QScreen::grabWindow()
 */
class AvFoundationCapture : public QObject, public IScreenCapture {
    Q_OBJECT

public:
    AvFoundationCapture();
    ~AvFoundationCapture() override;

    AvFoundationCapture(const AvFoundationCapture&) = delete;
    AvFoundationCapture& operator=(const AvFoundationCapture&) = delete;

    [[nodiscard]] bool initialize(int outputIndex = 0) override;
    void shutdown() override;
    [[nodiscard]] bool isInitialized() const override;
    CaptureResult captureFrame(int timeoutMs = 100) override;
    [[nodiscard]] CursorMessage sampleCursorPosition() const override;
    [[nodiscard]] QSize desktopSize() const override;
    [[nodiscard]] QString lastError() const override;
    [[nodiscard]] bool reinitialize() override;

    /// 运行时检测 ScreenCaptureKit 可用性（macOS 13.0+）
    [[nodiscard]] static bool isAvailable();

signals:
    /// 屏幕录制权限缺失时发射，调用方可弹出系统偏好设置引导
    void screenCapturePermissionRequired();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    QSize   m_desktopSize;
    bool    m_initialized = false;
    QString m_lastError;
};

#endif // Q_OS_MACOS
