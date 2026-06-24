#pragma once

#ifndef QT_NO_OPENGL

#include <QtGui/qopengl.h>

/**
 * @brief 解码输出的 GPU 纹理写入目标
 *
 * 封装 PBO 双缓冲、持久映射、纹理管理等 GL 细节。
 * 解码器通过此接口写入解码结果，无需了解纹理/PBO 内部实现。
 *
 * 线程安全：
 *   - mapWriteBuffer / commitWriteBuffer / uploadPixels → DecodeWorker 线程
 *   - swapDisplay / displayTexture → GUI 线程（paintGL）
 *
 * 所有权：由 GLTextureViewport 创建和销毁。
 */
class IDecodeTarget {
public:
    virtual ~IDecodeTarget() = default;

    /// GPU 零拷贝路径：获取当前帧的可写 GPU 缓冲区（PBO 映射指针）。
    /// 解码器可直接写入解码像素，跳过 CPU 中转。
    /// @param width  图像宽度（像素）
    /// @param height 图像高度（像素）
    /// @return 已映射的可写指针（width * height * 3 字节），失败返回 nullptr
    [[nodiscard]] virtual unsigned char* mapWriteBuffer(int width, int height) = 0;

    /// 提交 GPU 写入：解除 PBO 映射 → glTexSubImage2D 到后台纹理 → 创建 GLsync fence。
    /// 仅在 mapWriteBuffer 成功后调用。
    /// @return GLsync fence 供 GUI 线程同步，失败返回 nullptr
    [[nodiscard]] virtual GLsync commitWriteBuffer() = 0;

    /// CPU 路径：将已解码的 RGB 像素上传到后台 GL 纹理。
    /// 内部执行：memcpy 到 PBO → glTexSubImage2D → glFenceSync
    /// @param data   RGB 像素数据（width * height * 3 字节，无行填充）
    /// @param width  图像宽度
    /// @param height 图像高度
    /// @return GLsync fence，失败返回 nullptr
    [[nodiscard]] virtual GLsync uploadPixels(const unsigned char* data,
                                               int width, int height) = 0;

    /// GUI 线程调用：交换显示纹理索引。
    /// Worker 写入后台纹理 → swapDisplay() 将其提升为显示纹理 → paintGL 渲染。
    virtual void swapDisplay() = 0;

    /// 获取当前显示纹理 ID（GUI 线程，paintGL 调用）
    [[nodiscard]] virtual GLuint displayTexture() const = 0;

    /// 当前纹理尺寸
    [[nodiscard]] virtual int textureWidth() const = 0;
    [[nodiscard]] virtual int textureHeight() const = 0;

    // ── CL/GL interop 接口（GPU 零拷贝路径）──

    /// 确保 PBO 和纹理已分配（CL/GL interop 路径不走 mapWriteBuffer 触发分配）
    /// 在调用 writablePboId() 之前调用。
    /// @param width  图像宽度
    /// @param height 图像高度
    /// @return PBO 分配成功返回 true
    [[nodiscard]] virtual bool ensureBufferReady(int width, int height)
    { Q_UNUSED(width); Q_UNUSED(height); return true; }

    /// 返回当前可写入 PBO 的 GL ID（0 = CL/GL interop 不可用）
    /// OpenCL 通过 clCreateFromGLBuffer 直接写入该 PBO，跳过 CPU 回读。
    [[nodiscard]] virtual GLuint writablePboId() const { return 0; }

    /// 从 OpenCL interop 提交（PBO 已由 CL 直接写入）
    /// 内部：绑定 PBO → glTexSubImage2D → fence sync（不解映射——CL 写入而非 CPU 映射）
    [[nodiscard]] virtual GLsync commitFromInterop(int width, int height)
    { Q_UNUSED(width); Q_UNUSED(height); return nullptr; }
};

#endif // QT_NO_OPENGL
