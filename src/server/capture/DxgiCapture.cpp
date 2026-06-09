#ifdef _WIN32

#include "DxgiCapture.h"
#include "../../common/core/logging/LoggingCategories.h"

#include <algorithm>
#include <dxgi.h>
#include <d3d11.h>
#include <dxgi1_2.h>

// Link libraries (redundant with CMake, but helps IDE intellisense)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

DxgiCapture::DxgiCapture() = default;

DxgiCapture::~DxgiCapture() {
    shutdown();
}

bool DxgiCapture::initialize(int outputIndex) {
    // Clean up any previous state
    shutdown();
    m_outputIndex = outputIndex;

    qCInfo(lcDxgiCapture) << "Initializing DXGI capture engine, output index:" << outputIndex;

    if ( !createD3DDevice() ) {
        return false;
    }

    if ( !acquireOutputDuplication(outputIndex) ) {
        shutdown();
        return false;
    }

    m_initialized = true;
    qCInfo(lcDxgiCapture) << "DXGI capture engine initialized successfully, desktop size:"
        << m_desktopSize.width() << "x" << m_desktopSize.height();
    return true;
}

void DxgiCapture::shutdown() {
    // 仅 m_initialized 守卫：Reset 后 COM 指针可能不归零，
    // 联合判断 !m_device && !m_duplication 不够可靠。
    if ( !m_initialized ) {
        return;
    }

    // Release in reverse order of creation
    m_stagingTexture.Reset();
    m_duplication.Reset();
    m_context.Reset();
    m_device.Reset();

    m_initialized = false;
    m_desktopSize = QSize();

    qCDebug(lcDxgiCapture) << "DXGI capture engine shut down";
}

bool DxgiCapture::reinitialize() {
    const int savedOutputIndex = m_outputIndex;
    qCInfo(lcDxgiCapture) << "Reinitializing DXGI capture (output:" << savedOutputIndex << ")";
    shutdown();
    return initialize(savedOutputIndex);
}

bool DxgiCapture::createD3DDevice() {
    // Feature levels to try (we only need basic 2D, so 11.0 is fine)
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL featureLevel{};

    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
    // Enable D3D debug layer in debug builds (helps catch API misuse)
    // Only if the SDK debug layer is installed
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // Default adapter
        D3D_DRIVER_TYPE_HARDWARE,   // Hardware GPU
        nullptr,                    // No software rasterizer
        createFlags,
        featureLevels,
        static_cast<UINT>(std::size(featureLevels)),
        D3D11_SDK_VERSION,
        m_device.GetAddressOf(),
        &featureLevel,
        m_context.GetAddressOf()
    );

    // If debug layer is not available, retry without it
    if ( FAILED(hr) && (createFlags & D3D11_CREATE_DEVICE_DEBUG) ) {
        createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
            featureLevels, static_cast<UINT>(std::size(featureLevels)),
            D3D11_SDK_VERSION,
            m_device.GetAddressOf(), &featureLevel, m_context.GetAddressOf()
        );
    }

    if ( FAILED(hr) ) {
        m_lastError = QString("D3D11CreateDevice failed: HRESULT 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCCritical(lcDxgiCapture) << m_lastError;
        return false;
    }

    qCDebug(lcDxgiCapture) << "D3D11 device created, feature level:"
        << QString("0x%1").arg(featureLevel, 0, 16);
    return true;
}

bool DxgiCapture::acquireOutputDuplication(int outputIndex) {
    // Get DXGI device from D3D11 device
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_device.As(&dxgiDevice);
    if ( FAILED(hr) ) {
        m_lastError = QString("QueryInterface for IDXGIDevice failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCCritical(lcDxgiCapture) << m_lastError;
        return false;
    }

    // Get the adapter (GPU)
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if ( FAILED(hr) ) {
        m_lastError = QString("GetAdapter failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCCritical(lcDxgiCapture) << m_lastError;
        return false;
    }

    // Enumerate outputs to find the requested monitor
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(static_cast<UINT>(outputIndex), output.GetAddressOf());
    if ( FAILED(hr) ) {
        m_lastError = QString("EnumOutputs(%1) failed: 0x%2 — monitor may not exist")
            .arg(outputIndex)
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCCritical(lcDxgiCapture) << m_lastError;
        return false;
    }

    // Get output description for desktop size
    DXGI_OUTPUT_DESC outputDesc{};
    hr = output->GetDesc(&outputDesc);
    if ( FAILED(hr) ) {
        m_lastError = QString("GetDesc failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCCritical(lcDxgiCapture) << m_lastError;
        return false;
    }

    m_desktopSize = QSize(
        outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left,
        outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top
    );

    // QI for IDXGIOutput1 (Desktop Duplication requires DXGI 1.2)
    Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if ( FAILED(hr) ) {
        m_lastError = QString("QueryInterface for IDXGIOutput1 failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCCritical(lcDxgiCapture) << m_lastError;
        return false;
    }

    // Acquire desktop duplication
    hr = output1->DuplicateOutput(m_device.Get(), m_duplication.GetAddressOf());
    if ( FAILED(hr) ) {
        m_lastError = QString("DuplicateOutput failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));

        if ( hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE ) {
            m_lastError += " — maximum number of Desktop Duplication sessions reached";
        } else if ( hr == DXGI_ERROR_UNSUPPORTED ) {
            m_lastError += " — Desktop Duplication not supported (e.g., Remote Desktop session)";
        } else if ( hr == E_ACCESSDENIED ) {
            m_lastError += " — access denied (secure desktop or UAC)";
        }

        qCCritical(lcDxgiCapture) << m_lastError;
        return false;
    }

    // Create initial staging texture
    if ( !createStagingTexture(m_desktopSize.width(), m_desktopSize.height()) ) {
        m_duplication.Reset();
        return false;
    }

    qCInfo(lcDxgiCapture) << "Desktop Duplication acquired for output"
        << outputIndex << "size:" << m_desktopSize;
    return true;
}

bool DxgiCapture::createStagingTexture(int width, int height) {
    D3D11_TEXTURE2D_DESC stagingDesc{};
    stagingDesc.Width = static_cast<UINT>(width);
    stagingDesc.Height = static_cast<UINT>(height);
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // Desktop Duplication always uses BGRA
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.SampleDesc.Quality = 0;
    stagingDesc.Usage = D3D11_USAGE_STAGING;           // CPU-readable
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    m_stagingTexture.Reset();
    HRESULT hr = m_device->CreateTexture2D(&stagingDesc, nullptr,
        m_stagingTexture.GetAddressOf());
    if ( FAILED(hr) ) {
        m_lastError = QString("CreateTexture2D (staging) failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCCritical(lcDxgiCapture) << m_lastError;
        return false;
    }

    qCDebug(lcDxgiCapture) << "Staging texture created:" << width << "x" << height;
    return true;
}

QImage DxgiCapture::captureFrame(int timeoutMs) {
    if ( !m_initialized || !m_duplication ) {
        m_lastError = "DXGI capture not initialized";
        return QImage();
    }

    // Acquire the next desktop frame
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;

    HRESULT hr = m_duplication->AcquireNextFrame(
        static_cast<UINT>(timeoutMs), &frameInfo, desktopResource.GetAddressOf());

    if ( hr == DXGI_ERROR_WAIT_TIMEOUT ) {
        // No new frame within timeout — not an error, just no update
        return QImage();
    }

    if ( hr == DXGI_ERROR_ACCESS_LOST ) {
        m_lastError = "Desktop Duplication access lost (desktop switch/resolution change)";
        qCWarning(lcDxgiCapture) << m_lastError;
        m_initialized = false;  // Caller should call reinitialize()
        return QImage();
    }

    if ( FAILED(hr) ) {
        m_lastError = QString("AcquireNextFrame failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCWarning(lcDxgiCapture) << m_lastError;
        return QImage();
    }

    // Get the ID3D11Texture2D from the acquired resource
    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if ( FAILED(hr) ) {
        m_duplication->ReleaseFrame();
        m_lastError = QString("QueryInterface for ID3D11Texture2D failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCWarning(lcDxgiCapture) << m_lastError;
        return QImage();
    }

    // Check if desktop size changed (resolution change while duplication is active)
    D3D11_TEXTURE2D_DESC texDesc{};
    desktopTexture->GetDesc(&texDesc);
    const QSize currentSize(static_cast<int>(texDesc.Width),
                            static_cast<int>(texDesc.Height));
    if ( currentSize != m_desktopSize ) {
        qCInfo(lcDxgiCapture) << "Desktop size changed from" << m_desktopSize
            << "to" << currentSize;
        m_desktopSize = currentSize;
        if ( !createStagingTexture(m_desktopSize.width(), m_desktopSize.height()) ) {
            m_duplication->ReleaseFrame();
            return QImage();
        }
    }

    // Copy GPU texture → staging texture (GPU→CPU transfer)
    m_context->CopyResource(m_stagingTexture.Get(), desktopTexture.Get());

    // Map staging texture to CPU memory
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = m_context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if ( FAILED(hr) ) {
        m_duplication->ReleaseFrame();
        m_lastError = QString("Map staging texture failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCWarning(lcDxgiCapture) << m_lastError;
        return QImage();
    }

    // Create QImage from mapped data
    // Desktop Duplication uses BGRA format, which maps to QImage::Format_RGB32
    // (Qt's Format_RGB32 is 0xffRRGGBB in native byte order, which is BGRA on little-endian)
    // We must deep-copy because the mapped pointer is only valid until Unmap()
    const int width = m_desktopSize.width();
    const int height = m_desktopSize.height();

    QImage image(width, height, QImage::Format_RGB32);

    const auto* srcRow = static_cast<const uint8_t*>(mapped.pData);
    for ( int y = 0; y < height; ++y ) {
        memcpy(image.scanLine(y), srcRow, static_cast<size_t>(width) * 4);
        srcRow += mapped.RowPitch;
    }

    // Unmap and release
    m_context->Unmap(m_stagingTexture.Get(), 0);
    m_duplication->ReleaseFrame();

    return image;
}

QVector<DirtyRect> DxgiCapture::mergeDirtyRects(
    const QVector<QRect>&                   rawDirtyRects,
    const QVector<DXGI_OUTDUPL_MOVE_RECT>&  rawMoveRects,
    int                                     mergeThreshold,
    const QSize&                            desktopSize) {
    Q_UNUSED(desktopSize);

    QVector<DirtyRect> result;

    // 将脏矩形转为可合并列表
    struct MergeRect {
        QRect r;
        bool  merged = false;
    };
    QVector<MergeRect> rects;
    rects.reserve(rawDirtyRects.size() + rawMoveRects.size());
    for (const auto& r : rawDirtyRects) {
        rects.append({r, false});
    }

    // 将移动矩形作为 DirtyRect 直接加入（含移动信息）
    for (const auto& m : rawMoveRects) {
        DirtyRect dr;
        dr.rect = QRect(m.DestinationRect.left, m.DestinationRect.top,
            m.DestinationRect.right  - m.DestinationRect.left,
            m.DestinationRect.bottom - m.DestinationRect.top);
        dr.isMoveRect = true;
        dr.moveSrc = QPoint(m.SourcePoint.x, m.SourcePoint.y);
        result.append(dr);
    }

    // 迭代合并：反复合并重叠/相邻矩形直至稳定
    auto mergePass = [&](int threshold) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < rects.size(); ++i) {
                if (rects[i].merged) continue;
                for (int j = i + 1; j < rects.size(); ++j) {
                    if (rects[j].merged) continue;
                    QRect grown = rects[i].r.adjusted(-threshold, -threshold, threshold, threshold);
                    if (grown.intersects(rects[j].r)) {
                        rects[i].r = rects[i].r.united(rects[j].r);
                        rects[j].merged = true;
                        changed = true;
                    }
                }
            }
        }
    };

    // 第一遍：按 mergeThreshold 合并
    mergePass(mergeThreshold);

    // 将未被合并的脏矩形加入结果
    for (const auto& r : rects) {
        if (!r.merged) {
            DirtyRect dr;
            dr.rect = r.r;
            result.append(dr);
        }
    }

    // 过滤：移除被移动矩形完全覆盖的脏矩形
    result.erase(std::remove_if(result.begin(), result.end(),
        [&](const DirtyRect& d) {
            if (d.isMoveRect) return false;
            for (const auto& mv : result) {
                if (!mv.isMoveRect) continue;
                if (mv.rect.contains(d.rect)) return true;
            }
            return false;
        }),
        result.end());

    // 若矩形数量超过 16 个，清空（调用方应回退全帧模式）
    if (result.size() > 16) {
        result.clear();
    }

    return result;
}

CaptureResult DxgiCapture::captureFrameWithDirtyRects(int timeoutMs) {
    CaptureResult result;
    if (!m_initialized || !m_duplication) {
        m_lastError = QStringLiteral("DXGI capture not initialized");
        return result;
    }

    // 1. AcquireNextFrame
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;

    HRESULT hr = m_duplication->AcquireNextFrame(
        static_cast<UINT>(timeoutMs), &frameInfo, desktopResource.GetAddressOf());

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return result;  // 无新帧
    }

    if (hr == DXGI_ERROR_ACCESS_LOST) {
        m_lastError = QStringLiteral("Desktop Duplication access lost");
        qCWarning(lcDxgiCapture) << m_lastError;
        m_initialized = false;
        return result;
    }

    if (FAILED(hr)) {
        m_lastError = QStringLiteral("AcquireNextFrame failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCWarning(lcDxgiCapture) << m_lastError;
        return result;
    }

    // 2. 获取桌面纹理
    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (FAILED(hr)) {
        m_duplication->ReleaseFrame();
        return result;
    }

    // 3. 从 DXGI 获取脏矩形和移动矩形
    UINT dirtyRectsBufferSize = 0;
    UINT moveRectsBufferSize = 0;
    m_duplication->GetFrameDirtyRects(0, nullptr, &dirtyRectsBufferSize);
    m_duplication->GetFrameMoveRects(0, nullptr, &moveRectsBufferSize);

    QVector<RECT> rawDirtyRects;
    QVector<DXGI_OUTDUPL_MOVE_RECT> rawMoveRects;

    const UINT totalDirty = dirtyRectsBufferSize / sizeof(RECT);
    const UINT totalMove  = moveRectsBufferSize / sizeof(DXGI_OUTDUPL_MOVE_RECT);

    if (totalDirty > 0) {
        rawDirtyRects.resize(static_cast<int>(totalDirty));
        m_duplication->GetFrameDirtyRects(dirtyRectsBufferSize,
            rawDirtyRects.data(), &dirtyRectsBufferSize);
    }

    if (totalMove > 0) {
        rawMoveRects.resize(static_cast<int>(totalMove));
        m_duplication->GetFrameMoveRects(moveRectsBufferSize,
            rawMoveRects.data(), &moveRectsBufferSize);
    }

    // 4. 转换为 QRect
    QVector<QRect> qDirtyRects;
    qDirtyRects.reserve(rawDirtyRects.size());
    for (const auto& r : rawDirtyRects) {
        qDirtyRects.append(QRect(r.left, r.top,
            r.right - r.left, r.bottom - r.top));
    }

    // 5. 合并相邻脏矩形（阈值 32px，消除碎片化以减少下游管线条目数）
    const int mergeThreshold = 32;
    QVector<DirtyRect> merged = mergeDirtyRects(qDirtyRects, rawMoveRects,
        mergeThreshold, m_desktopSize);

    // 6. 判断是否需要全帧模式
    //   (a) merged 为空：DXGI 未提供脏矩形
    //   (b) 总面积 > 70%：回退全帧更高效
    //   (c) 脏矩形 > 6 个：碎片化开销超过单帧编码开销，回退全帧
    qint64 totalDirtyArea = 0;
    for (const auto& dr : merged) {
        totalDirtyArea += static_cast<qint64>(dr.rect.width()) * dr.rect.height();
    }
    const qint64 totalScreenArea = static_cast<qint64>(m_desktopSize.width())
                                 * m_desktopSize.height();

    const bool useFullFrame = merged.isEmpty()
        || (totalDirtyArea > totalScreenArea * 7 / 10)
        || (merged.size() > 6);  // 过多碎片的开销超过单帧编码

    // 7. 检查/调整桌面尺寸
    D3D11_TEXTURE2D_DESC texDesc{};
    desktopTexture->GetDesc(&texDesc);
    const QSize currentSize(static_cast<int>(texDesc.Width),
                            static_cast<int>(texDesc.Height));
    if (currentSize != m_desktopSize) {
        qCInfo(lcDxgiCapture) << "Desktop size changed from" << m_desktopSize
            << "to" << currentSize;
        m_desktopSize = currentSize;
        if (!createStagingTexture(m_desktopSize.width(), m_desktopSize.height())) {
            m_duplication->ReleaseFrame();
            return result;
        }
    }

    // 8. 复制到 staging 并映射
    m_context->CopyResource(m_stagingTexture.Get(), desktopTexture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = m_context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        m_duplication->ReleaseFrame();
        m_lastError = QStringLiteral("Map staging texture failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        return result;
    }

    // 9. 从映射数据创建 QImage（深拷贝，因为 Unmap 后指针失效）
    result.fullImage = QImage(m_desktopSize.width(), m_desktopSize.height(),
        QImage::Format_RGB32);
    const auto* srcRow = static_cast<const uint8_t*>(mapped.pData);
    for (int y = 0; y < m_desktopSize.height(); ++y) {
        memcpy(result.fullImage.scanLine(y), srcRow,
            static_cast<size_t>(m_desktopSize.width()) * 4);
        srcRow += mapped.RowPitch;
    }

    m_context->Unmap(m_stagingTexture.Get(), 0);
    m_duplication->ReleaseFrame();

    // 10. 设置结果
    result.isFullFrame = useFullFrame;
    result.desktopSize = m_desktopSize;
    result.dirtyRects  = useFullFrame ? QVector<DirtyRect>() : std::move(merged);

    return result;
}

#endif // _WIN32
