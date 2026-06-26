#pragma once

#ifdef _WIN32

#include <QtGui/QImage>
#include <QtCore/QSize>
#include <QtCore/QRect>
#include <QtCore/QString>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>  // Microsoft::WRL::ComPtr

#include "../../common/core/network/Protocol.h"

/**
 * @brief Result of a single capture operation.
 *
 * Combines the frame image and cursor shape data, both extracted within
 * a single AcquireNextFrame / ReleaseFrame window.
 * - frame.isNull() = no new frame (timeout or error)
 * - cursor.width == 0 = cursor unchanged or hidden
 */
struct CaptureResult {
    QImage        frame;
    CursorMessage cursor;
};

/**
 * @brief DXGI Desktop Duplication capture engine.
 *
 * Wraps the Windows DXGI 1.2 Desktop Duplication API for GPU-accelerated
 * screen capture. Frames are captured as GPU textures and copied to a
 * CPU-readable staging texture, then converted to QImage.
 *
 * Performance characteristics:
 * - AcquireNextFrame: ~0.1ms (GPU signals new frame)
 * - GPU→CPU copy (MapSubresource): ~0.3ms for 1080p, ~1ms for 4K
 * - Total per-frame: <1ms for 1080p vs ~10-15ms for QScreen::grabWindow()
 *
 * Lifecycle: create → initialize() → captureFrame() loop → shutdown()
 *
 * Thread safety: NOT thread-safe. All methods must be called from the
 * same thread (the ScreenCaptureWorker's thread).
 */
class DxgiCapture {
public:
    DxgiCapture();
    ~DxgiCapture();

    // Non-copyable, non-movable (COM resources)
    DxgiCapture(const DxgiCapture&) = delete;
    DxgiCapture& operator=(const DxgiCapture&) = delete;
    DxgiCapture(DxgiCapture&&) = delete;
    DxgiCapture& operator=(DxgiCapture&&) = delete;

    /**
     * @brief Initialize D3D11 device and DXGI output duplication.
     *
     * Creates the D3D11 device, enumerates adapters/outputs, and
     * calls IDXGIOutput1::DuplicateOutput() to start duplication.
     *
     * @param outputIndex  Monitor index (0 = primary). Default: 0.
     * @return true on success, false on failure (call lastError() for details).
     */
    bool initialize(int outputIndex = 0);

    /**
     * @brief Release all D3D11/DXGI resources.
     *
     * Safe to call multiple times. After shutdown(), initialize() can
     * be called again to re-acquire.
     */
    void shutdown();

    /**
     * @brief Check if the engine is initialized and ready to capture.
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Capture the current desktop frame.
     *
     * Calls AcquireNextFrame with the given timeout. If a new frame is
     * available, copies it to a staging texture and maps it to CPU memory,
     * producing a QImage (Format_RGB32). Also extracts the cursor shape
     * when available.
     *
     * @param timeoutMs  Max wait time for a new frame (ms). Default: 100.
     * @return CaptureResult with frame and cursor; frame.isNull() on timeout/error.
     */
    CaptureResult captureFrame(int timeoutMs = 100);

    /**
     * @brief Get the desktop dimensions being captured.
     */
    QSize desktopSize() const { return m_desktopSize; }

    /**
     * @brief Get the last error message.
     */
    QString lastError() const { return m_lastError; }

    /**
     * @brief Reinitialize after access-lost error (DXGI_ERROR_ACCESS_LOST).
     *
     * Desktop Duplication can be lost when the desktop switches (e.g.,
     * UAC prompt, lock screen, resolution change). This method calls
     * shutdown() then initialize() with the same output index.
     *
     * @return true if reinitialization succeeded.
     */
    bool reinitialize();

private:
    /**
     * @brief Create D3D11 device with appropriate feature level.
     */
    bool createD3DDevice();

    /**
     * @brief Enumerate DXGI adapters and outputs, acquire duplication.
     */
    bool acquireOutputDuplication(int outputIndex);

    /**
     * @brief Create or recreate the CPU-readable staging texture.
     *
     * Called when the desktop size changes (resolution switch).
     */
    bool createStagingTexture(int width, int height);

    /**
     * @brief Extract cursor shape from current duplicated frame.
     *
     * Must be called between AcquireNextFrame() and ReleaseFrame().
     * Uses GetFramePointerShape to retrieve cursor pixels, converts
     * to RGBA, and applies SHA-1 change detection to avoid duplicates.
     *
     * @return CursorMessage with RGBA pixels, or empty (width == 0) if unchanged/hidden.
     */
    CursorMessage extractCursorShape(const DXGI_OUTDUPL_FRAME_INFO& frameInfo);

    // D3D11 / DXGI COM objects
    Microsoft::WRL::ComPtr<ID3D11Device>           m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>     m_context;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication>  m_duplication;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_stagingTexture;

    // State
    bool    m_initialized = false;
    bool    m_comInitialized = false;  // 追踪 COM 是否由本类初始化（与 CoInitializeEx 配对）
    int     m_outputIndex = 0;
    QSize   m_desktopSize;
    QString m_lastError;

    // 缓存上次成功提取的光标形状（形状不变时复用，仅更新位置）
    CursorMessage m_cachedCursor;
};

#endif // _WIN32
