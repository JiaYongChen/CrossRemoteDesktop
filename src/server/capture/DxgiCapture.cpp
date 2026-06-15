#ifdef _WIN32

#include "DxgiCapture.h"
#include "../../common/core/logging/LoggingCategories.h"

#include <dxgi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <objbase.h>  // CoInitializeEx, CoUninitialize, RPC_E_CHANGED_MODE

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
    // Idempotent: only release COM objects if we hold any
    if ( m_initialized || m_device || m_duplication ) {
        // Release in reverse order of creation
        m_stagingTexture.Reset();
        m_duplication.Reset();
        m_context.Reset();
        m_device.Reset();

        m_initialized = false;
        m_desktopSize = QSize();

        qCDebug(lcDxgiCapture) << "DXGI capture engine shut down";
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
    qCInfo(lcDxgiCapture) << "Reinitializing DXGI capture (output:" << savedOutputIndex << ")";
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
            qCCritical(lcDxgiCapture) << m_lastError;
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

#endif // _WIN32
