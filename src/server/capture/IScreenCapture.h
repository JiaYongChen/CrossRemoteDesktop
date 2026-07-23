#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <memory>

#include "../../common/network/Protocol.h"  // CursorMessage

/**
 * @brief Result of a single capture operation.
 *
 * Combines the frame image and cursor shape data, both extracted within
 * a single capture window.
 * - frame.isNull() = no new frame (timeout or error)
 * - cursor.width == 0 = cursor unchanged or hidden
 */
struct CaptureResult {
    QImage        frame;
    CursorMessage cursor;
};

/**
 * @brief 屏幕捕获抽象接口
 *
 * 定义平台无关的屏幕捕获操作接口。Windows 平台通过 DxgiCapture 实现，
 * 其他平台返回 nullptr（降级为 Qt GDI 回退路径）。
 *
 * 生命周期：createScreenCapture() → initialize() → captureFrame() loop → shutdown()
 *
 * 线程安全：非线程安全，所有方法必须从同一线程调用。
 */
class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;

    // Non-copyable, non-movable
    IScreenCapture(const IScreenCapture&) = delete;
    IScreenCapture& operator=(const IScreenCapture&) = delete;
    IScreenCapture(IScreenCapture&&) = delete;
    IScreenCapture& operator=(IScreenCapture&&) = delete;

    /** @brief 初始化捕获引擎 */
    [[nodiscard]] virtual bool initialize(int outputIndex = 0) = 0;

    /** @brief 释放所有资源（可多次调用） */
    virtual void shutdown() = 0;

    /** @brief 检查引擎是否已初始化并准备就绪 */
    [[nodiscard]] virtual bool isInitialized() const = 0;

    /** @brief 捕获当前桌面帧 */
    virtual CaptureResult captureFrame(int timeoutMs = 100) = 0;

    /** @brief 独立于帧捕获的光标位置采样（轻量，仅 GetCursorPos + 缓存形状） */
    [[nodiscard]] virtual CursorMessage sampleCursorPosition() const = 0;

    /** @brief 获取正在捕获的桌面尺寸 */
    [[nodiscard]] virtual QSize desktopSize() const = 0;

    /** @brief 获取最后的错误消息 */
    [[nodiscard]] virtual QString lastError() const = 0;

    /** @brief 在 access-lost 错误后重新初始化 */
    [[nodiscard]] virtual bool reinitialize() = 0;

protected:
    IScreenCapture() = default;
};

/**
 * @brief 创建屏幕捕获引擎实例的工厂函数。
 *
 * Windows 上返回 DxgiCapture 实例，其他平台返回 nullptr（调用方降级为 GDI 回退）。
 */
std::unique_ptr<IScreenCapture> createScreenCapture();
