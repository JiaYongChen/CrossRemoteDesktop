#ifdef _WIN32

#include "DxgiCapture.h"
#include "../../common/core/logging/LoggingCategories.h"

#include <vector>
#include <dxgi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <objbase.h>  // CoInitializeEx, CoUninitialize, RPC_E_CHANGED_MODE

#include <QDataStream>


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

    qCInfo(lcServerCaptureDxgi) << "Initializing DXGI capture engine, output index:" << outputIndex;

    if ( !createD3DDevice() ) {
        return false;
    }

    if ( !acquireOutputDuplication(outputIndex) ) {
        shutdown();
        return false;
    }

    m_initialized = true;
    qCInfo(lcServerCaptureDxgi) << "DXGI capture engine initialized successfully, desktop size:"
        << m_desktopSize.width() << "x" << m_desktopSize.height();
    return true;
}

void DxgiCapture::shutdown() {
    // Idempotent: only release COM objects if we hold any
    if ( m_initialized || m_device || m_duplication ) {
        // Release in reverse order of creation
        m_stagingTexture.Reset();
        m_duplication.Reset();
        m_context.Reset();
        m_device.Reset();

        m_initialized = false;
        m_desktopSize = QSize();

        qCDebug(lcServerCaptureDxgi) << "DXGI capture engine shut down";
    }

    // ─────────────────────────────────────────────────────────
    // 配对 CoInitializeEx：在所有 D3D/DXGI COM 对象释放后调用。
    //
    // m_comInitialized 仅在 CoInitializeEx 返回 S_OK 时为 true，
    // 避免错误反初始化由 Qt 或系统初始化的 COM 公寓。
    // ─────────────────────────────────────────────────────────
    if ( m_comInitialized ) {
        CoUninitialize();
        m_comInitialized = false;
    }
}

bool DxgiCapture::reinitialize() {
    const int savedOutputIndex = m_outputIndex;
    qCInfo(lcServerCaptureDxgi) << "Reinitializing DXGI capture (output:" << savedOutputIndex << ")";
    shutdown();
    return initialize(savedOutputIndex);
}

bool DxgiCapture::createD3DDevice() {
    // ─────────────────────────────────────────────────────────
    // 显式初始化 COM 公寓（Apartment）。
    //
    // D3D11CreateDevice 内部依赖 COM，若线程尚未初始化 COM，
    // 系统会隐式初始化但不会自动配对 CoUninitialize。
    // 缺少显式反初始化会导致 COM 后台线程残留，阻塞
    // QApplication 析构阶段的 QPA 屏幕子系统清理（\\.\DISPLAY1），
    // 最终造成终端窗口挂死不退出。
    // ─────────────────────────────────────────────────────────
    HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comHr)) {
        // RPC_E_CHANGED_MODE：COM 已被其他组件以不同线程模型初始化。
        // COM 仍然可用，不应视为致命错误。
        if (comHr != RPC_E_CHANGED_MODE) {
            m_lastError = QString("CoInitializeEx failed: HRESULT 0x%1")
                .arg(static_cast<unsigned long>(comHr), 8, 16, QChar('0'));
            qCWarning(lcServerCaptureDxgi) << m_lastError;
            return false;
        }
    }
    m_comInitialized = !FAILED(comHr);  // S_OK 或 S_FALSE 均要求配对 CoUninitialize

    // Feature levels to try (we only need basic 2D, so 11.0 is fine)
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL featureLevel{};

    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    // ── D3D11_CREATE_DEVICE_DEBUG 修复 ──
    // D3D11_CREATE_DEVICE_DEBUG 加载 debug layer DLL（D3D11SDKLayers.dll）。
    // 首次加载需要 5-10 秒（尤其老旧 GPU 如 GTX 650 + NVIDIA 472.12）。
    // 由于该标志仅用于开发期 API 校验（非功能必需），其代价远超收益：
    // 首次连接 DXGI 初始化耗时 7+ 秒 → 用户看到黑屏 → 误判为故障 → 关闭窗口。
    // 修复：仅在 API 调用失败时回退到不带 DEBUG 标志重试，而非默认启用。
    // 开发者需要调试时可临时手动打开此标志。
    //
    // #ifndef NDEBUG
    //     createFlags |= D3D11_CREATE_DEVICE_DEBUG;
    // #endif

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
        qCWarning(lcServerCaptureDxgi) << m_lastError;
        return false;
    }

    qCDebug(lcServerCaptureDxgi) << "D3D11 device created, feature level:"
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
        qCWarning(lcServerCaptureDxgi) << m_lastError;
        return false;
    }

    // Get the adapter (GPU)
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if ( FAILED(hr) ) {
        m_lastError = QString("GetAdapter failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCWarning(lcServerCaptureDxgi) << m_lastError;
        return false;
    }

    // Enumerate outputs to find the requested monitor
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(static_cast<UINT>(outputIndex), output.GetAddressOf());
    if ( FAILED(hr) ) {
        m_lastError = QString("EnumOutputs(%1) failed: 0x%2 — monitor may not exist")
            .arg(outputIndex)
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCWarning(lcServerCaptureDxgi) << m_lastError;
        return false;
    }

    // Get output description for desktop size
    DXGI_OUTPUT_DESC outputDesc{};
    hr = output->GetDesc(&outputDesc);
    if ( FAILED(hr) ) {
        m_lastError = QString("GetDesc failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCWarning(lcServerCaptureDxgi) << m_lastError;
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
        qCWarning(lcServerCaptureDxgi) << m_lastError;
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

        qCWarning(lcServerCaptureDxgi) << m_lastError;
        return false;
    }

    // Create initial staging texture
    if ( !createStagingTexture(m_desktopSize.width(), m_desktopSize.height()) ) {
        m_duplication.Reset();
        return false;
    }

    qCInfo(lcServerCaptureDxgi) << "Desktop Duplication acquired for output"
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
        qCWarning(lcServerCaptureDxgi) << m_lastError;
        return false;
    }

    qCDebug(lcServerCaptureDxgi) << "Staging texture created:" << width << "x" << height;
    return true;
}

CaptureResult DxgiCapture::captureFrame(int timeoutMs) {
    if ( !m_initialized || !m_duplication ) {
        m_lastError = "DXGI capture not initialized";
        return CaptureResult{};
    }

    // Acquire the next desktop frame
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;

    HRESULT hr = m_duplication->AcquireNextFrame(
        static_cast<UINT>(timeoutMs), &frameInfo, desktopResource.GetAddressOf());

    if ( hr == DXGI_ERROR_WAIT_TIMEOUT ) {
        // 屏幕内容未变化，但仍需提取光标位置（鼠标移动不触发 AcquireNextFrame）
        CaptureResult result;
        DXGI_OUTDUPL_FRAME_INFO zeroInfo{};
        result.cursor = extractCursorShape(zeroInfo);
        return result;
    }

    if ( hr == DXGI_ERROR_ACCESS_LOST ) {
        m_lastError = "Desktop Duplication access lost (desktop switch/resolution change)";
        qCWarning(lcServerCaptureDxgi) << m_lastError;
        m_initialized = false;  // Caller should call reinitialize()
        return CaptureResult{};
    }

    if ( FAILED(hr) ) {
        m_lastError = QString("AcquireNextFrame failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCWarning(lcServerCaptureDxgi) << m_lastError;
        return CaptureResult{};
    }

    // Get the ID3D11Texture2D from the acquired resource
    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if ( FAILED(hr) ) {
        m_duplication->ReleaseFrame();
        m_lastError = QString("QueryInterface for ID3D11Texture2D failed: 0x%1")
            .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'));
        qCWarning(lcServerCaptureDxgi) << m_lastError;
        return CaptureResult{};
    }

    // Check if desktop size changed (resolution change while duplication is active)
    D3D11_TEXTURE2D_DESC texDesc{};
    desktopTexture->GetDesc(&texDesc);
    const QSize currentSize(static_cast<int>(texDesc.Width),
                            static_cast<int>(texDesc.Height));
    if ( currentSize != m_desktopSize ) {
        qCInfo(lcServerCaptureDxgi) << "Desktop size changed from" << m_desktopSize
            << "to" << currentSize;
        m_desktopSize = currentSize;
        if ( !createStagingTexture(m_desktopSize.width(), m_desktopSize.height()) ) {
            m_duplication->ReleaseFrame();
            return CaptureResult{};
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
        qCWarning(lcServerCaptureDxgi) << m_lastError;
        return CaptureResult{};
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

    // Unmap
    m_context->Unmap(m_stagingTexture.Get(), 0);

    // Extract cursor before releasing the frame
    CaptureResult result;
    result.frame  = image;
    result.cursor = extractCursorShape(frameInfo);

    m_duplication->ReleaseFrame();
    return result;
}

CursorMessage DxgiCapture::extractCursorShape(const DXGI_OUTDUPL_FRAME_INFO& frameInfo) {
    CursorMessage msg;
    static int s_extractCount = 0;
    ++s_extractCount;

    // LastMouseUpdateTime==0 表示此帧无新光标数据。
    // 不能用此条件跳过——光标可能只有位置变化无形状变化。
    // 回退到缓存形状 + 当前鼠标位置。
    if (frameInfo.LastMouseUpdateTime.QuadPart == 0) {
        if (s_extractCount <= 3)
            qCDebug(lcServerCaptureDxgi) << "extractCursorShape #" << s_extractCount
                << ": no new pointer data, fallback to cached shape";
        if (m_cachedCursor.width > 0) {
            POINT cursorPos{};
            if (GetCursorPos(&cursorPos)) {
                msg = m_cachedCursor;
                msg.posX = cursorPos.x;
                msg.posY = cursorPos.y;
                return msg;
            }
        }
        return msg;
    }

    // 预分配 256KB 缓冲区（光标最大 256×256×4=256KB）
    // 部分驱动/硬件组合不支持零缓冲区查询
    constexpr UINT kMaxCursorSize = 256 * 1024;
    std::vector<BYTE> buffer(kMaxCursorSize);
    UINT bufferSize = kMaxCursorSize;
    DXGI_OUTDUPL_POINTER_SHAPE_INFO shapeInfo{};
    HRESULT hr = m_duplication->GetFramePointerShape(bufferSize, buffer.data(),
                                                      &bufferSize, &shapeInfo);

    if (hr == DXGI_ERROR_MORE_DATA) {
        // 光标比预分配的大（极端情况），重新分配
        buffer.resize(bufferSize);
        hr = m_duplication->GetFramePointerShape(bufferSize, buffer.data(),
                                                  &bufferSize, &shapeInfo);
    }

    if (FAILED(hr)) {
        // GetFramePointerShape 仅在光标形状变化时返回数据。
        // 失败时回退到缓存形状 + 当前鼠标位置（形状不变仅移动也需发送）。
        if (s_extractCount <= 3)
            qCDebug(lcServerCaptureDxgi) << "extractCursorShape #" << s_extractCount
                << ": GetFramePointerShape FAILED hr=0x" << Qt::hex << (unsigned long)hr
                << "— fallback to cached shape";
        if (m_cachedCursor.width > 0) {
            POINT cursorPos{};
            if (GetCursorPos(&cursorPos)) {
                msg = m_cachedCursor;
                msg.posX = cursorPos.x;
                msg.posY = cursorPos.y;
                return msg;
            }
        }
        return msg;
    }
    if (shapeInfo.Width == 0 || shapeInfo.Height == 0) {
        if (s_extractCount <= 3)
            qCDebug(lcServerCaptureDxgi) << "extractCursorShape #" << s_extractCount
                << ": cursor hidden (0x0), type=" << shapeInfo.Type;
        return msg;
    }

    // 防范 DXGI 返回未初始化内存（部分驱动/GPU 组合下 GetFramePointerShape
    // 返回 S_OK 但 shapeInfo 含垃圾值，如 Debug 版 MSVC 的 0xCCCCCCCC）
    if (shapeInfo.Width < 0 || shapeInfo.Width > 512 ||
        shapeInfo.Height < 0 || shapeInfo.Height > 512) {
        if (s_extractCount <= 5)
            qCDebug(lcServerCaptureDxgi) << "extractCursorShape #" << s_extractCount
                << ": REJECTED garbage size" << shapeInfo.Width << "x" << shapeInfo.Height;
        return msg;
    }

    // 获取服务端光标屏幕坐标
    POINT cursorPos{};
    if (GetCursorPos(&cursorPos)) {
        msg.posX = cursorPos.x;
        msg.posY = cursorPos.y;
    }

    // 转换 → RGBA
    msg.width  = shapeInfo.Width;
    msg.height = shapeInfo.Height;
    msg.hotX   = shapeInfo.HotSpot.x;
    msg.hotY   = shapeInfo.HotSpot.y;

    int pixelCount = msg.width * msg.height;
    msg.pixels.resize(pixelCount * 4);
    BYTE* dst = reinterpret_cast<BYTE*>(msg.pixels.data());

    switch (shapeInfo.Type) {
    case 1: { // DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME
        // 单色光标: AND mask (1bpp) + XOR mask (1bpp)
        // 行对齐到 32-bit
        int rowBytes = ((msg.width + 31) / 32) * 4;
        BYTE* andMask = buffer.data() + rowBytes * msg.height;
        for (int y = 0; y < msg.height; ++y) {
            for (int x = 0; x < msg.width; ++x) {
                int byteIdx = (y * rowBytes) + (x / 8);
                int bitIdx  = 7 - (x % 8);
                bool andBit = (buffer[byteIdx] >> bitIdx) & 1;
                bool xorBit = (andMask[byteIdx] >> bitIdx) & 1;
                int idx = (y * msg.width + x) * 4;
                if (!andBit && xorBit) { dst[idx+2]=255; dst[idx+1]=255; dst[idx]=255; dst[idx+3]=255; }
                else if (!andBit && !xorBit) { dst[idx+3]=0; }
                else if (andBit && !xorBit) { dst[idx+3]=0; }
                else { dst[idx+2]=0; dst[idx+1]=0; dst[idx]=0; dst[idx+3]=255; }
            }
        }
        break;
    }
    case 2: // DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR (32bpp ARGB)
    case 4: // DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR (32bpp XOR + AND mask)
        // 直接复制 ARGB → RGBA
        {
            BYTE* src = buffer.data();
            for (int i = 0; i < pixelCount; ++i) {
                dst[i*4]   = src[i*4+2];  // R ← B
                dst[i*4+1] = src[i*4+1];  // G
                dst[i*4+2] = src[i*4];    // B ← R
                dst[i*4+3] = src[i*4+3];  // A
            }
        }
        break;
    }

    // 缓存形状（GetFramePointerShape 仅形状变化时成功，后续帧用缓存+新位置）
    m_cachedCursor = msg;

    // 无条件发送：每次有效光标提取都推送到客户端
    static int s_cursorDiag = 0;
    ++s_cursorDiag;
    if (s_cursorDiag <= 3)
        qCDebug(lcServerCaptureDxgi) << "extractCursorShape #" << s_cursorDiag
            << ": cursor" << msg.width << "x" << msg.height
            << "hot:" << msg.hotX << "," << msg.hotY
            << "pos:" << msg.posX << "," << msg.posY;
    return msg;
}

CursorMessage DxgiCapture::sampleCursorPosition() const {
    CursorMessage msg;
    if (m_cachedCursor.width > 0) {
        POINT cursorPos{};
        if (GetCursorPos(&cursorPos)) {
            msg = m_cachedCursor;
            msg.posX = cursorPos.x;
            msg.posY = cursorPos.y;
        }
    }
    return msg;
}

#endif // _WIN32
