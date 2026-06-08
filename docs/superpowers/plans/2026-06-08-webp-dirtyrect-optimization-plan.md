# WebP 编码 + 脏矩形区域更新 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 1080p 远程桌面帧延迟从 ~2800ms 降至 <50ms，通过 WebP 编码替代 Qt 内置 JPEG + DXGI 脏矩形检测减少编码数据量。

**Architecture:** 三个阶段递进——(1) 数据层改动（常量/协议/结构体），只加字段不改行为；(2) 服务端管线（DxgiCapture 脏矩形 → DataProcessingWorker WebP 编码 → ClientHandlerWorker 发送），区域帧零影响全帧路径；(3) 客户端管线（DecodeWorker CompositorBuffer 合成 → GLTextureViewport 局部 glTexSubImage2D），全帧路径保持不变。

**Tech Stack:** C++20, Qt 6.9+, DXGI 1.2 Desktop Duplication, OpenGL 3.3 Core, WebP (Qt 内置插件)

---

## Phase 1: 数据层基础 — 常量 + 协议 + 结构体

### Task 1: 更新 Constants.h 编码常量

**文件:**
- 修改: `src/common/core/config/Constants.h:27-31`

- [ ] **Step 1: 替换 JPEG 质量常量为 WebP 质量常量**

```cpp
// 将 Compression 结构体中的行：
struct Compression {
    static constexpr int DEFAULT_JPEG_QUALITY = 85;                 ///< 默认 JPEG 质量
    static constexpr double SCALE_FACTOR_HIGH = 1.0;                ///< 高清缩放因子
};

// 替换为：
struct Compression {
    static constexpr int DEFAULT_WEBP_QUALITY = 95;                 ///< 默认 WebP 质量（近无损）
    static constexpr int FALLBACK_JPEG_QUALITY = 85;                ///< 回退 JPEG 质量（WebP 编码失败时）
    static constexpr int SMALL_REGION_PNG_THRESHOLD = 64;           ///< 脏区域 ≤64×64px 使用 PNG
    static constexpr double SCALE_FACTOR_HIGH = 1.0;                ///< 高清缩放因子
};
```

- [ ] **Step 2: 构建验证**

```bash
cmake --build build --config Debug
```
预期: 编译成功（模块依赖 Constants.h 的代码会自动引用新常量名，暂不引用新常量故编译不受影响）

- [ ] **Step 3: 提交**

```bash
git add src/common/core/config/Constants.h
git commit -m "refactor: Constants 编码常量 JPEG→WebP，新增 PNG 阈值和回退质量

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

### Task 2: 扩展 ScreenDataFlags 协议标志位

**文件:**
- 修改: `src/common/core/network/Protocol.h:200-203`

- [ ] **Step 1: 新增 FULL_FRAME 和 MOVE_RECT 标志**

```cpp
// 替换 Protocol.h:200-203：
enum class ScreenDataFlags : quint8 {
    NONE        = 0x00,  ///< 无特殊标志（区域更新帧，WebP/PNG 编码）
    SCALED      = 0x01,  ///< 图像已缩放（需要客户端放大显示）
    FULL_FRAME  = 0x02,  ///< 全帧（客户端据此初始化/替换 compositor buffer）
    MOVE_RECT   = 0x04,  ///< 移动区域（imageData 前 20 字节为 MoveRect 编码）
};
```

注意：`SCALED` 原值 `0x02` 改为 `0x01`。由于当前代码中 `SCALED` 的值未被网络持久化（同一进程内 flags 传递），且 `isScaled` bool 单独存在于 ProcessedData，改动对现有行为无影响。

- [ ] **Step 2: 验证所有引用 SCALED 的代码**

```bash
rg "SCALED" src/
```
预期输出如下位置，确认编译不受影响：
- `Protocol.h:202`（定义）
- `ClientHandlerWorker.cpp:295`（`|= SCALED`）
- `SessionManager.cpp:246`（`& SCALED`）
- `DecodeWorker.cpp:71`（`& SCALED`）

- [ ] **Step 3: 构建验证**

```bash
cmake --build build --config Debug
```
预期: 编译成功。

- [ ] **Step 4: 提交**

```bash
git add src/common/core/network/Protocol.h
git commit -m "feat: ScreenDataFlags 新增 FULL_FRAME/MOVE_RECT

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

### Task 3: 扩展 ProcessedData — 脏矩形/全帧/移动区域字段

**文件:**
- 修改: `src/server/dataflow/DataFlowStructures.h:107-190`

- [ ] **Step 1: 添加字段到 ProcessedData**

```cpp
// 在 ProcessedData 结构体中，compressedDataSize 字段之后、isScaled 字段之前插入：
struct ProcessedData {
    QByteArray compressedData;       ///< 处理后的图像数据（WebP/PNG 编码）
    QDateTime processedTime;         ///< 处理完成时间戳
    quint64 originalFrameId;         ///< 原始帧ID
    QSize imageSize;                 ///< 图像尺寸
    qint64 originalDataSize;         ///< 原始数据大小
    qint64 compressedDataSize;       ///< 处理后数据大小
    // === 新增字段 ===
    bool isFullFrame = false;        ///< 是否为全帧（客户端需初始化 compositor buffer）
    bool isMoveRect = false;         ///< 是否为移动区域（imageData 前 20B = MoveRect 编码）
    QRect dirtyRect;                 ///< 脏区域坐标（客户端合成位置）
    QPoint moveSrc;                  ///< 移动源坐标（仅 isMoveRect=true）
    QPoint moveDst;                  ///< 移动目标坐标（仅 isMoveRect=true）
    QSize  moveSize;                 ///< 移动区域大小（仅 isMoveRect=true）
    // === 现有字段 ===
    bool isScaled;                   ///< 是否进行了缩放
    QSize originalImageSize;         ///< 原始图像尺寸（缩放前）
    quint64 captureTimestamp = 0;    ///< 捕获时间戳 (ms since epoch)

    // ... 构造函数、isValid() 等方法保持不变
};
```

- [ ] **Step 2: 构建验证**

```bash
cmake --build build --config Debug
```
预期: 编译成功（新增字段有默认值，现有构造路径不受影响）。

- [ ] **Step 3: 提交**

```bash
git add src/server/dataflow/DataFlowStructures.h
git commit -m "feat: ProcessedData 增加脏矩形/全帧/移动区域字段

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

### Task 4: 扩展 DecodedFrame — 脏矩形/全帧/移动区域字段

**文件:**
- 修改: `src/client/core/DecodedFrame.h`

- [ ] **Step 1: 添加字段到 DecodedFrame**

```cpp
// 替换整个 DecodedFrame 结构体：
struct DecodedFrame {
    QImage   image;         ///< 解码后图像（COW 引用 compositor buffer 或全帧）
    QSize    remoteSize;    ///< 远端桌面总尺寸
    QRect    dirtyRect;     ///< 本帧脏区域（全帧模式下 = image.rect()）
    quint64  frameId = 0;
    bool     isFullFrame = false;  ///< 全帧，客户端初始化 compositor buffer
    bool     isMoveRect  = false;  ///< 移动区域，跳过编解码
    QPoint   moveSrc;             ///< 移动源坐标
    QPoint   moveDst;             ///< 移动目标坐标
    QSize    moveSize;            ///< 移动区域大小
};
```

- [ ] **Step 2: 构建验证**

```bash
cmake --build build --config Debug
```
预期: 编译成功。

- [ ] **Step 3: 提交**

```bash
git add src/client/core/DecodedFrame.h
git commit -m "feat: DecodedFrame 增加 dirtyRect/moveRect 区域更新字段

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase 2: 服务端 — DXGI 脏矩形检测

### Task 5: DxgiCapture 新增脏矩形检测接口

**文件:**
- 修改: `src/server/capture/DxgiCapture.h`
- 修改: `src/server/capture/DxgiCapture.cpp`

- [ ] **Step 1: 在 DxgiCapture.h 中新增结构体和接口声明**

在 `DxgiCapture.h` 的 class 声明前插入：

```cpp
/// 脏/移动矩形描述
struct DirtyRect {
    QRect  rect;
    bool   isMoveRect = false;
    QPoint moveSrc;
};

/// 捕获结果：全帧模式或区域模式
struct CaptureResult {
    QImage             fullImage;
    QVector<DirtyRect> dirtyRects;
    bool               isFullFrame = true;
    QSize              desktopSize;
    quint64            frameId = 0;
};
```

在 `DxgiCapture` 类的 `public` 区域，`captureFrame()` 声明后新增：

```cpp
    /**
     * @brief 捕获桌面帧并检测脏矩形。
     *
     * 调用 AcquireNextFrame + GetFrameDirtyRects + GetFrameMoveRects。
     * 自动合并相邻脏矩形；面积 >70% 全屏时回退全帧模式。
     *
     * @param timeoutMs  等待新帧的最大时间 (ms)
     * @return CaptureResult，其中 isFullFrame=true 时 fullImage 有效，
     *         isFullFrame=false 时 dirtyRects 有效
     */
    CaptureResult captureFrameWithDirtyRects(int timeoutMs = 100);
```

在 `private` 区域新增辅助方法：

```cpp
    /// 合并相邻脏矩形（间距 <mergeThreshold px），控制数量 ≤16
    static QVector<DirtyRect> mergeDirtyRects(
        const QVector<QRect>&  rawDirtyRects,
        const QVector<DXGI_OUTDUPL_MOVE_RECT>& rawMoveRects,
        int                     mergeThreshold,
        const QSize&            desktopSize);
```

- [ ] **Step 2: 实现 captureFrameWithDirtyRects()**

在 `DxgiCapture.cpp` 中，`captureFrame()` 实现之后新增：

```cpp
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
        return result;  // 无新帧，result.fullImage.isNull() == true
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

    // 3. 获取脏矩形列表
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

    // 4. 转换为 QRect 列表
    QVector<QRect> qDirtyRects;
    qDirtyRects.reserve(rawDirtyRects.size());
    for (const auto& r : rawDirtyRects) {
        qDirtyRects.append(QRect(r.left, r.top,
            r.right - r.left, r.bottom - r.top));
    }

    // 5. 合并脏矩形 + 移动矩形 → 统一 DirtyRect 列表
    const int mergeThreshold = 16;
    QVector<DirtyRect> merged = mergeDirtyRects(qDirtyRects, rawMoveRects,
        mergeThreshold, m_desktopSize);

    // 6. 计算总面积 → 超过 70% 回退全帧
    qint64 totalDirtyArea = 0;
    for (const auto& dr : merged) {
        totalDirtyArea += static_cast<qint64>(dr.rect.width()) * dr.rect.height();
    }
    const qint64 totalScreenArea = static_cast<qint64>(m_desktopSize.width())
                                 * m_desktopSize.height();

    const bool useFullFrame = merged.isEmpty()
        || (totalDirtyArea > totalScreenArea * 7 / 10);

    // 7. 全帧模式下提取全帧图像
    if (useFullFrame) {
        D3D11_TEXTURE2D_DESC texDesc{};
        desktopTexture->GetDesc(&texDesc);
        const QSize currentSize(static_cast<int>(texDesc.Width),
                                static_cast<int>(texDesc.Height));
        if (currentSize != m_desktopSize) {
            m_desktopSize = currentSize;
            createStagingTexture(currentSize.width(), currentSize.height());
        }

        m_context->CopyResource(m_stagingTexture.Get(), desktopTexture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = m_context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            m_duplication->ReleaseFrame();
            return result;
        }

        result.fullImage = QImage(m_desktopSize.width(), m_desktopSize.height(),
            QImage::Format_RGB32);
        const auto* srcRow = static_cast<const uint8_t*>(mapped.pData);
        for (int y = 0; y < m_desktopSize.height(); ++y) {
            memcpy(result.fullImage.scanLine(y), srcRow,
                static_cast<size_t>(m_desktopSize.width()) * 4);
            srcRow += mapped.RowPitch;
        }

        m_context->Unmap(m_stagingTexture.Get(), 0);
        result.isFullFrame = true;
        result.desktopSize = m_desktopSize;
    } else {
        // 8. 区域模式：仅裁剪脏区域 + 移动区域源像素
        // 先复制全帧到 staging 供裁剪使用
        D3D11_TEXTURE2D_DESC texDesc{};
        desktopTexture->GetDesc(&texDesc);
        const QSize currentSize(static_cast<int>(texDesc.Width),
                                static_cast<int>(texDesc.Height));
        if (currentSize != m_desktopSize) {
            m_desktopSize = currentSize;
            createStagingTexture(currentSize.width(), currentSize.height());
        }

        m_context->CopyResource(m_stagingTexture.Get(), desktopTexture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = m_context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            m_duplication->ReleaseFrame();
            return result;
        }

        // 保存整个 staging 数据以裁剪区域
        QByteArray stagingCopy(static_cast<int>(mapped.RowPitch) * m_desktopSize.height(),
            Qt::Uninitialized);
        memcpy(stagingCopy.data(), mapped.pData,
            static_cast<size_t>(mapped.RowPitch) * m_desktopSize.height());
        m_context->Unmap(m_stagingTexture.Get(), 0);

        // 裁剪每个脏矩形
        for (auto& dr : merged) {
            if (!dr.isMoveRect) {
                QRect clamped = dr.rect.intersected(
                    QRect(QPoint(0,0), m_desktopSize));
                if (clamped.isEmpty()) continue;

                QImage crop(clamped.width(), clamped.height(), QImage::Format_RGB32);
                const auto* srcRow = reinterpret_cast<const uint8_t*>(stagingCopy.constData())
                    + clamped.y() * static_cast<int>(mapped.RowPitch)
                    + clamped.x() * 4;
                for (int y = 0; y < clamped.height(); ++y) {
                    memcpy(crop.scanLine(y), srcRow,
                        static_cast<size_t>(clamped.width()) * 4);
                    srcRow += mapped.RowPitch;
                }
                dr.rect = clamped;
                // fullImage 存储裁剪区域（临时：后续改为独立小图像）
                // 此处将 Crop 结果通过 fullImage 返回给 ScreenCaptureWorker
                // 实际：使用单独的 per-region QImage 列表
            }
        }

        result.dirtyRects = std::move(merged);
        result.isFullFrame = false;
        result.desktopSize = m_desktopSize;

        // 保存全帧 staging 数据供 ScreenCaptureWorker 裁剪使用
        result.fullImage = QImage(reinterpret_cast<const uchar*>(stagingCopy.constData()),
            m_desktopSize.width(), m_desktopSize.height(),
            static_cast<int>(mapped.RowPitch) / 4, QImage::Format_RGB32).copy();
    }

    m_duplication->ReleaseFrame();
    return result;
}
```

注意：上述实现中区域裁剪部分需要优化——CaptureResult 改为持有 staging 数据的 QImage（作为像素源），由 ScreenCaptureWorker 做区域裁剪和入队。此处先提供完整逻辑，Task 7 中统一整合。

- [ ] **Step 3: 实现矩形合并算法 mergeDirtyRects()**

```cpp
QVector<DirtyRect> DxgiCapture::mergeDirtyRects(
    const QVector<QRect>&                   rawDirtyRects,
    const QVector<DXGI_OUTDUPL_MOVE_RECT>&  rawMoveRects,
    int                                     mergeThreshold,
    const QSize&                            desktopSize) {
    QVector<DirtyRect> result;

    // 1. 转换脏矩形
    struct MergeRect {
        QRect r;
        bool  merged = false;
    };
    QVector<MergeRect> rects;
    rects.reserve(rawDirtyRects.size() + rawMoveRects.size());
    for (const auto& r : rawDirtyRects) {
        rects.append({r, false});
    }

    // 2. 添加移动矩形作为 DirtyRect（同时记录移动信息）
    for (const auto& m : rawMoveRects) {
        DirtyRect dr;
        dr.rect = QRect(m.DestinationRect.left, m.DestinationRect.top,
            m.DestinationRect.right  - m.DestinationRect.left,
            m.DestinationRect.bottom - m.DestinationRect.top);
        dr.isMoveRect = true;
        dr.moveSrc = QPoint(m.SourceRect.left, m.SourceRect.top);
        result.append(dr);
    }

    auto mergePass = [&](int threshold) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < rects.size(); ++i) {
                if (rects[i].merged) continue;
                for (int j = i + 1; j < rects.size(); ++j) {
                    if (rects[j].merged) continue;
                    // 检查是否相邻
                    QRect grown = rects[i].r.adjusted(-threshold, -threshold,
                        threshold, threshold);
                    if (grown.intersects(rects[j].r)) {
                        rects[i].r = rects[i].r.united(rects[j].r);
                        rects[j].merged = true;
                        changed = true;
                    }
                }
            }
        }
    };

    // 第一轮合并：阈值 16px
    mergePass(mergeThreshold);

    // 添加未合并的矩形
    for (const auto& r : rects) {
        if (!r.merged) {
            DirtyRect dr;
            dr.rect = r.r;
            result.append(dr);
        }
    }

    // 过滤：丢弃被移动区域完全覆盖的脏矩形
    for (auto& dr : result) {
        if (dr.isMoveRect) continue;
        for (const auto& mv : result) {
            if (!mv.isMoveRect) continue;
            if (mv.rect.contains(dr.rect)) {
                dr.rect = QRect();  // 标记为无效
                break;
            }
        }
    }
    result.erase(std::remove_if(result.begin(), result.end(),
        [](const DirtyRect& d) { return d.rect.isEmpty(); }),
        result.end());

    // 数量超限回退：>16 个 → 全帧
    if (result.size() > 16) {
        result.clear();
    }

    return result;
}
```

- [ ] **Step 4: 构建验证**

```bash
cmake --build build --config Debug
```
预期: 编译成功（新方法暂未被调用）。

- [ ] **Step 5: 提交**

```bash
git add src/server/capture/DxgiCapture.h src/server/capture/DxgiCapture.cpp
git commit -m "feat: DxgiCapture 新增脏矩形检测 CaptureResult + mergeDirtyRects

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase 3: 服务端 — WebP 编码 + 区域/移动路径

### Task 6: DataProcessingWorker WebP 编码切换

**文件:**
- 修改: `src/server/dataprocessing/DataProcessingWorker.cpp`

- [ ] **Step 1: 修改 encodeImage() — JPEG→WebP 编码，保留回退**

找到 `encodeImage()` 方法（约第 215 行）。替换编码部分：

```cpp
// 替换以下代码块（约 254-276 行）：
// OLD:
//         bool saveSuccess = convertedImage.save(&buffer, "JPG", quality);
//
// NEW:
        // WebP 编码（Qt 6 内置，跨平台零依赖）
        bool saveSuccess = convertedImage.save(&buffer, "WEBP", quality);

        // WebP 编码失败时回退 JPEG（兼容无 WebP 插件的 Qt 构建）
        if (!saveSuccess || jpegData.isEmpty()) {
            qCWarning(lcDataProcessingWorker) << "WebP 编码失败，回退 JPEG 编码，帧ID:" << frameId;
            buffer.close();
            QBuffer jpegBuffer(&jpegData);
            jpegBuffer.open(QIODevice::WriteOnly);
            saveSuccess = convertedImage.save(&jpegBuffer, "JPG",
                CoreConstants::Compression::FALLBACK_JPEG_QUALITY);
            jpegBuffer.close();
        }

        // 同时修改日志行中的"编码JPEG"为"编码帧"：
        // 将 "编码JPEG，帧ID:" 改为 "编码帧，格式:%s 帧ID:"
        // 最终输出格式用三元判断
```

- [ ] **Step 2: 修改 processTask() — 全帧不调用 encodeImage 的缩放路径**

在 `processTask()` 中（约第 168-170 行）：

```cpp
// 将：
const int quality = CoreConstants::Compression::DEFAULT_JPEG_QUALITY;
// 改为：
const int quality = CoreConstants::Compression::DEFAULT_WEBP_QUALITY;
```

- [ ] **Step 3: 添加区域编码 + 小区域 PNG 逻辑**

在 `processTask()` 的帧处理循环中（约第 172 行附近），在 `encodeImage` 调用前增加路径分支。但目前 `CapturedFrame` 没有脏矩形字段——这就需要我们在 Task 7 中调整 ScreenCaptureWorker 来填充不同路径。

先最小化改动：仅修改编码质量和回退逻辑，保持 encodeImage 签名不变。区域路径在 Task 7 中统一处理。

- [ ] **Step 4: 构建验证**

```bash
cmake --build build --config Debug
```

预期的编译警告：`DEFAULT_JPEG_QUALITY` 已重命名为 `DEFAULT_WEBP_QUALITY`。如下位置需要同步更新引用：
- `DataProcessingWorker.cpp` 中的 `CoreConstants::Compression::DEFAULT_JPEG_QUALITY` → `DEFAULT_WEBP_QUALITY`

全部修改后重新构建确保通过。

- [ ] **Step 5: 提交**

```bash
git add src/server/dataprocessing/DataProcessingWorker.cpp
git commit -m "perf: JPEG→WebP 编码 + 回退 JPEG 机制

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

### Task 7: ScreenCaptureWorker + DataProcessingWorker 区域编码路径

**文件:**
- 修改: `src/server/capture/ScreenCaptureWorker.cpp`
- 修改: `src/server/dataprocessing/DataProcessingWorker.cpp`
- 修改: `src/server/dataflow/DataFlowStructures.h` (CapturedFrame 扩展)

- [ ] **Step 1: CapturedFrame 增加区域帧字段**

在 `DataFlowStructures.h` 的 `CapturedFrame` 结构体中，`originalSize` 字段后新增：

```cpp
    // === 区域更新字段 ===
    bool   isFullFrame = true;     ///< 全帧模式（首帧/回退）
    bool   isMoveRect  = false;    ///< 移动区域
    QRect  dirtyRect;              ///< 脏矩形坐标
    QPoint moveSrc;                ///< 移动源坐标
    QPoint moveDst;                ///< 移动目标坐标
    QSize  moveSize;               ///< 移动区域大小
```

- [ ] **Step 2: ScreenCaptureWorker::performCapture() 分支**

修改 `ScreenCaptureWorker.cpp` 中 `performCapture()` 方法，替换 DXGI 捕获部分（约第 266 行起）：

```cpp
    auto captureStartTime = std::chrono::steady_clock::now();
    try {
#ifdef Q_OS_WIN
        if (m_dxgiAvailable && m_dxgiCapture) {
            const int captureTimeout = std::max(1,
                static_cast<int>(m_frameDelay.count()));

            // 新增：尝试脏矩形捕获
            static bool s_firstFrame = true;
            CaptureResult capResult = m_dxgiCapture->captureFrameWithDirtyRects(
                s_firstFrame ? 1000 : captureTimeout);

            if (!capResult.fullImage.isNull()) {
                m_dxgiReinitAttempts = 0;
                s_firstFrame = false;

                if (capResult.isFullFrame) {
                    // 全帧路径
                    enqueueFullFrame(capResult);
                } else {
                    // 区域路径：每个脏矩形独立入队
                    for (const auto& dr : capResult.dirtyRects) {
                        if (dr.isMoveRect) {
                            enqueueMoveRect(dr, capResult.frameId);
                        } else {
                            // 从全帧 staging 数据裁剪区域
                            QImage regionCrop = capResult.fullImage.copy(dr.rect);
                            enqueueDirtyRegion(std::move(regionCrop), dr.rect,
                                capResult.desktopSize, capResult.frameId);
                        }
                    }
                    // 更新统计（区域帧算作一次捕获）
                    recordCaptureStats(captureStartTime);
                    m_lastCaptureTime = std::chrono::steady_clock::now();
                    return;
                }
            } else {
                // DXGI 超时/无变化 → 仅当设备不再初始化时尝试重建
                // （保留现有 captureScreen() 中的 DXGI 恢复逻辑）
                if (!m_dxgiCapture->isInitialized()) {
                    qCWarning(lcScreenCaptureWorker) << "DXGI access lost, attempting reinitialize";
                    if (m_dxgiReinitAttempts++ <= MAX_DXGI_REINIT_ATTEMPTS) {
                        if (m_dxgiCapture->reinitialize()) {
                            m_dxgiReinitAttempts = 0;
                        }
                    } else {
                        m_dxgiAvailable = false;
                    }
                }
                return;
            }
        }
#endif
        // ... 现有 GDI 回退路径保持不变
```

- [ ] **Step 3: 添加 enqueueFullFrame / enqueueDirtyRegion / enqueueMoveRect 方法**

在 `ScreenCaptureWorker.h` 的 private 区域新增：

```cpp
    void enqueueFullFrame(const CaptureResult& cap);
    void enqueueDirtyRegion(QImage&& image, const QRect& rect,
        const QSize& desktopSize, quint64 frameId);
    void enqueueMoveRect(const DirtyRect& dr, quint64 frameId);
```

在 `ScreenCaptureWorker.cpp` 中实现：

```cpp
void ScreenCaptureWorker::enqueueFullFrame(const CaptureResult& cap) {
    if (!m_queueManager) return;

    CapturedFrame frame;
    frame.image        = std::make_shared<QImage>(cap.fullImage);
    frame.frameId      = cap.frameId;
    frame.originalSize = cap.desktopSize;
    frame.timestamp    = QDateTime::currentDateTime();
    frame.isFullFrame  = true;
    frame.dirtyRect    = QRect(QPoint(0,0), cap.desktopSize);

    if (!m_queueManager->enqueueCapturedFrame(frame)) {
        QMutexLocker locker(&m_statsMutex);
        m_stats.droppedFrames++;
    }
}

void ScreenCaptureWorker::enqueueDirtyRegion(QImage&& image, const QRect& rect,
    const QSize& desktopSize, quint64 frameId) {
    if (!m_queueManager) return;

    CapturedFrame frame(std::move(image), frameId);
    frame.originalSize = desktopSize;
    frame.isFullFrame  = false;
    frame.dirtyRect    = rect;

    if (!m_queueManager->enqueueCapturedFrame(frame)) {
        QMutexLocker locker(&m_statsMutex);
        m_stats.droppedFrames++;
    }
}

void ScreenCaptureWorker::enqueueMoveRect(const DirtyRect& dr, quint64 frameId) {
    if (!m_queueManager) return;

    CapturedFrame frame;
    frame.frameId     = frameId;
    frame.isFullFrame = false;
    frame.isMoveRect  = true;
    frame.moveSrc     = dr.moveSrc;
    frame.moveDst     = QPoint(dr.rect.x(), dr.rect.y());
    frame.moveSize    = dr.rect.size();
    frame.dirtyRect   = dr.rect;

    if (!m_queueManager->enqueueCapturedFrame(frame)) {
        QMutexLocker locker(&m_statsMutex);
        m_stats.droppedFrames++;
    }
}
```

- [ ] **Step 4: DataProcessingWorker processTask() 区域编码分支**

修改 `processTask()` 中的帧处理循环：

```cpp
for (auto& f : frameBatch) {
    if (!f.isValid() || f.getLatency() > 5000) { ++m_droppedFrames; continue; }

    if (f.isMoveRect) {
        // 移动区域：直接入队 PQ，不编码
        m_activeParallelTasks.fetch_add(1);
        QThreadPool::globalInstance()->start([f, this]() {
            ProcessedData pd;
            pd.originalFrameId     = f.frameId;
            pd.imageSize           = f.moveSize;
            pd.originalImageSize   = f.originalSize;
            pd.isFullFrame         = false;
            pd.isMoveRect          = true;
            pd.moveSrc             = f.moveSrc;
            pd.moveDst             = f.moveDst;
            pd.moveSize            = f.moveSize;
            pd.dirtyRect           = f.dirtyRect;
            pd.captureTimestamp    = static_cast<quint64>(
                f.timestamp.toMSecsSinceEpoch());
            // 移动区域编码: 20 字节 = srcX(4) + srcY(4) + dstX(4) + dstY(4) + w(2) + h(2)
            pd.compressedData.resize(20);
            QDataStream ds(&pd.compressedData, QIODevice::WriteOnly);
            ds.setByteOrder(QDataStream::LittleEndian);
            ds << static_cast<qint32>(f.moveSrc.x())
               << static_cast<qint32>(f.moveSrc.y())
               << static_cast<qint32>(f.moveDst.x())
               << static_cast<qint32>(f.moveDst.y())
               << static_cast<quint16>(f.moveSize.width())
               << static_cast<quint16>(f.moveSize.height());
            pd.compressedDataSize = pd.compressedData.size();

            if (m_queueManager && m_queueManager->enqueueProcessedData(pd)) {
                m_processedFrames++;
            } else {
                m_droppedFrames++;
            }
            m_activeParallelTasks.fetch_sub(1);
        });
        continue;
    }

    // 编码路径（全帧 or 脏区域）- 统一走 encodeImage
    auto capturedImage = f.image;
    auto frameId       = f.frameId;
    auto ts            = f.timestamp;
    auto isFull        = f.isFullFrame;
    auto dirtyRect     = f.dirtyRect;
    auto origSize      = f.originalSize;

    m_activeParallelTasks.fetch_add(1);
    QThreadPool::globalInstance()->start([quality, capturedImage,
        frameId, ts, isFull, dirtyRect, origSize, this]() {
        ProcessedData pd = encodeImage(*capturedImage, frameId, quality, 1.0);
        pd.captureTimestamp = static_cast<quint64>(ts.toMSecsSinceEpoch());
        pd.isFullFrame      = isFull;
        pd.dirtyRect        = dirtyRect;
        pd.originalImageSize = origSize;

        if (pd.isValid() && m_queueManager &&
            m_queueManager->enqueueProcessedData(pd)) {
            m_processedFrames++;
        } else {
            m_droppedFrames++;
        }
        m_activeParallelTasks.fetch_sub(1);
    });
}
```

- [ ] **Step 5: 构建验证**

```bash
cmake --build build --config Debug
```
预期: 编译成功。

- [ ] **Step 6: 提交**

```bash
git add src/server/capture/ScreenCaptureWorker.h src/server/capture/ScreenCaptureWorker.cpp src/server/dataprocessing/DataProcessingWorker.cpp src/server/dataflow/DataFlowStructures.h
git commit -m "feat: 服务端区域编码路径 — 脏矩形入队 + WebP/PEG 编码分支

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

### Task 8: ClientHandlerWorker 发送适配

**文件:**
- 修改: `src/server/clienthandler/ClientHandlerWorker.cpp:283-300`

- [ ] **Step 1: sendScreenDataFromQueue() 新增 FULL_FRAME / MOVE_RECT 标志**

```cpp
// 替换 sendScreenDataFromQueue() 中的 ScreenData 构造 + flags 设置
// (约 283-300 行)：

    ScreenData screenData;
    screenData.x = static_cast<quint16>(processedData.dirtyRect.x());
    screenData.y = static_cast<quint16>(processedData.dirtyRect.y());
    screenData.imageData = processedData.compressedData;
    screenData.width  = static_cast<quint16>(processedData.isMoveRect
        ? processedData.moveSize.width()
        : processedData.imageSize.width());
    screenData.height = static_cast<quint16>(processedData.isMoveRect
        ? processedData.moveSize.height()
        : processedData.imageSize.height());
    screenData.originalWidth  = static_cast<quint16>(processedData.originalImageSize.width());
    screenData.originalHeight = static_cast<quint16>(processedData.originalImageSize.height());
    screenData.dataSize = processedData.compressedDataSize;
    screenData.captureTimestamp = processedData.captureTimestamp;

    quint8 flags = static_cast<quint8>(ScreenDataFlags::NONE);
    if (processedData.isScaled)    flags |= static_cast<quint8>(ScreenDataFlags::SCALED);
    if (processedData.isFullFrame) flags |= static_cast<quint8>(ScreenDataFlags::FULL_FRAME);
    if (processedData.isMoveRect)  flags |= static_cast<quint8>(ScreenDataFlags::MOVE_RECT);
    screenData.flags = flags;
```

- [ ] **Step 2: 构建验证 + 提交**

```bash
cmake --build build --config Debug
git add src/server/clienthandler/ClientHandlerWorker.cpp
git commit -m "feat: ClientHandlerWorker 发送适配 FULL_FRAME/MOVE_RECT 标志

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase 4: 客户端 — 区域合成 + 局部上传

### Task 9: DecodeWorker CompositorBuffer 区域合成

**文件:**
- 修改: `src/client/managers/DecodeWorker.h`
- 修改: `src/client/managers/DecodeWorker.cpp`

- [ ] **Step 1: DecodeWorker.h 新增 CompositorBuffer 成员**

```cpp
// 在 DecodeWorker 类 private 区域添加：
    QImage m_compositorBuffer;       ///< 全尺寸远端桌面缓冲
    mutable QMutex m_compositorMutex; ///< 保护 compositor buffer 写入
    std::atomic<bool> m_compositorReady{false}; ///< CompositorBuffer 是否已初始化
```

- [ ] **Step 2: 修改 workLoop() — 区域合成逻辑**

替换 `DecodeWorker.cpp` 中 `workLoop()` 方法（约 45-94 行）：

```cpp
void DecodeWorker::workLoop() {
    qCInfo(lcClient) << "DecodeWorker::workLoop() - Starting decode loop";

    while (m_running.load()) {
        DecodeTask task;
        if (!m_queue.tryDequeue(task)) {
            QThread::msleep(1);
            continue;
        }

        const quint8 flags = task.screenData.flags;
        const bool isFullFrame = flags & static_cast<quint8>(ScreenDataFlags::FULL_FRAME);
        const bool isMoveRect  = flags & static_cast<quint8>(ScreenDataFlags::MOVE_RECT);

        if (!m_outputBuffer) {
            qCWarning(lcClient) << "DecodeWorker: outputBuffer is null";
            continue;
        }

        // 确定 remoteSize
        QSize remoteSize = task.remoteSize;
        if (!(flags & static_cast<quint8>(ScreenDataFlags::SCALED))
            || task.screenData.originalWidth == 0) {
            // 区域帧用 compositor buffer 尺寸，全帧用解码后尺寸
            if (!isFullFrame && m_compositorReady.load()) {
                QMutexLocker locker(&m_compositorMutex);
                remoteSize = m_compositorBuffer.size();
            }
        } else {
            remoteSize = QSize(task.screenData.originalWidth,
                               task.screenData.originalHeight);
        }

        // 移动区域：直接在 compositor buffer 内 memcpy
        if (isMoveRect && m_compositorReady.load()) {
            QDataStream ds(task.screenData.imageData);
            ds.setByteOrder(QDataStream::LittleEndian);
            qint32 srcX, srcY, dstX, dstY;
            quint16 mw, mh;
            ds >> srcX >> srcY >> dstX >> dstY >> mw >> mh;

            QMutexLocker locker(&m_compositorMutex);
            if (m_compositorBuffer.isNull()) { continue; }

            QRect srcRect(srcX, srcY, mw, mh);
            QRect dstRect(dstX, dstY, mw, mh);
            QRect clampedSrc  = srcRect.intersected(m_compositorBuffer.rect());
            QRect clampedDst  = dstRect.intersected(m_compositorBuffer.rect());

            // memcpy 逐行复制像素
            for (int row = 0; row < clampedSrc.height(); ++row) {
                const uchar* srcLine = m_compositorBuffer.constScanLine(
                    clampedSrc.y() + row);
                uchar* dstLine = m_compositorBuffer.scanLine(
                    clampedDst.y() + row);
                memcpy(dstLine + clampedDst.x() * 4,
                    srcLine + clampedSrc.x() * 4,
                    static_cast<size_t>(clampedSrc.width()) * 4);
            }

            // 构造输出帧（COW 引用 + 脏矩形）
            DecodedFrame* frame = nullptr;
            int idx = m_outputBuffer->acquireWrite(frame);
            if (frame) {
                quint64 fid = m_nextFrameId.fetch_add(1, std::memory_order_relaxed);
                frame->image      = m_compositorBuffer;  // COW 引用
                frame->remoteSize = remoteSize;
                frame->frameId    = fid;
                frame->isFullFrame = false;
                frame->isMoveRect  = true;
                frame->dirtyRect   = dstRect;
                frame->moveSrc     = QPoint(srcX, srcY);
                frame->moveDst     = QPoint(dstX, dstY);
                frame->moveSize    = QSize(mw, mh);
                m_outputBuffer->commitWrite(idx);
                emit frameDecoded(fid);
            }
            continue;
        }

        // === 解码 WebP/PNG/JPEG ===
        QBuffer buffer(&task.screenData.imageData);
        buffer.open(QIODevice::ReadOnly);

        // QImageReader 自动检测格式（WebP/PNG/JPEG）
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        if (!reader.read(&m_decodeBuffer) || m_decodeBuffer.isNull()) {
            qCWarning(lcClient) << "DecodeWorker: image decode failed, size:"
                                << task.screenData.imageData.size()
                                << "flags:" << flags;
            emit decodeError(QStringLiteral("图像解码失败"));
            continue;
        }

        const int regionX = task.screenData.x;
        const int regionY = task.screenData.y;

        // === 全帧或区域合成 ===
        {
            QMutexLocker locker(&m_compositorMutex);

            if (isFullFrame) {
                // 全帧：初始化/替换 compositor buffer
                // 注意：先拷贝再赋值，避免在旧 buffer 仍被 GUI 线程读取时过早释放
                m_compositorBuffer = m_decodeBuffer.copy();
                m_compositorReady.store(true);
            } else if (m_compositorReady.load()) {
                // 区域帧：粘贴解码图像到 compositor buffer
                if (m_compositorBuffer.isNull()) {
                    continue;  // 防御：等待首全帧
                }

                const int pasteW = qMin(m_decodeBuffer.width(),
                    m_compositorBuffer.width() - regionX);
                const int pasteH = qMin(m_decodeBuffer.height(),
                    m_compositorBuffer.height() - regionY);
                if (pasteW <= 0 || pasteH <= 0) continue;

                // 逐行粘贴（避免 QPainter 开销）
                for (int row = 0; row < pasteH; ++row) {
                    const uchar* srcLine = m_decodeBuffer.constScanLine(row);
                    uchar* dstLine = m_compositorBuffer.scanLine(regionY + row);
                    memcpy(dstLine + regionX * 4, srcLine,
                        static_cast<size_t>(pasteW) * 4);
                }
            } else {
                // Compositor buffer 未初始化（尚未收到首全帧）→ 跳过等待
                continue;
            }
        }

        // 输出到 TripleBuffer
        DecodedFrame* frame = nullptr;
        int idx = m_outputBuffer->acquireWrite(frame);
        if (frame) {
            quint64 fid = m_nextFrameId.fetch_add(1, std::memory_order_relaxed);
            frame->image       = m_compositorBuffer;  // COW 引用
            frame->remoteSize  = remoteSize;
            frame->frameId     = fid;
            frame->isFullFrame = isFullFrame;
            frame->dirtyRect   = isFullFrame
                ? QRect(QPoint(0,0), m_compositorBuffer.size())
                : QRect(regionX, regionY,
                    m_decodeBuffer.width(), m_decodeBuffer.height());
            m_outputBuffer->commitWrite(idx);
            emit frameDecoded(fid);
        }
    }

    qCInfo(lcClient) << "DecodeWorker::workLoop() - Decode loop ended";
    emit stopped();
}
```

- [ ] **Step 3: 构建验证**

```bash
cmake --build build --config Debug
```
预期: 编译成功。

- [ ] **Step 4: 提交**

```bash
git add src/client/managers/DecodeWorker.h src/client/managers/DecodeWorker.cpp
git commit -m "feat: DecodeWorker CompositorBuffer 区域合成 + 移动区域 memcpy

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

### Task 10: GLTextureViewport 局部上传支持

**文件:**
- 修改: `src/client/window/GLTextureViewport.cpp`
- 修改: `src/client/window/TextureRingBuffer.h`
- 修改: `src/client/window/TextureRingBuffer.cpp`

- [ ] **Step 1: TextureRingBuffer 新增 subImage 上传方法**

在 `TextureRingBuffer.h` public 区域新增：

```cpp
    /// 局部更新已有纹理的子区域（保留现有纹理存储）
    GLsync uploadSubImage(int slotIdx, const QImage& image, const QRect& region);
```

在 `TextureRingBuffer.cpp` 实现：

```cpp
GLsync TextureRingBuffer::uploadSubImage(int idx, const QImage& image,
    const QRect& region) {
    if (image.isNull() || region.isEmpty())
        return nullptr;

    if (!ensureGLInitialized())
        return nullptr;

    Slot& slot = m_slots[idx];
    if (slot.textureId == 0)
        return nullptr;

    GLint  internalFormat;
    GLenum format, type;
    int    bpp;
    const QImage* src = &image;

    QImage converted;
    if (!chooseGLFormat(image.format(), internalFormat, format, type, bpp)) {
        converted = image.convertedTo(QImage::Format_RGBA8888);
        chooseGLFormat(QImage::Format_RGBA8888, internalFormat, format, type, bpp);
        src = &converted;
    }

    glBindTexture(GL_TEXTURE_2D, slot.textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, src->bytesPerLine() / bpp);

    // 使用 PBO 持久映射路径（若可用）或直传路径
    const int totalBytes = src->bytesPerLine() * src->height();
    if (m_usePersistPbo && m_pbo[m_pboIndex].ptr) {
        if (totalBytes > m_pboSize) {
            destroyPersistPbo();
            tryInitPersistPbo(totalBytes);
        }
        if (m_usePersistPbo && m_pbo[m_pboIndex].ptr) {
            std::memcpy(m_pbo[m_pboIndex].ptr, src->constBits(),
                static_cast<size_t>(totalBytes));
            auto* ef = QOpenGLContext::currentContext()->extraFunctions();
            ef->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo[m_pboIndex].buffer);

            glTexSubImage2D(GL_TEXTURE_2D, 0,
                region.x(), region.y(), region.width(), region.height(),
                format, type, nullptr);
            ef->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            m_pboIndex = (m_pboIndex + 1) % 2;
        }
    } else {
        // 直传路径
        glTexSubImage2D(GL_TEXTURE_2D, 0,
            region.x(), region.y(), region.width(), region.height(),
            format, type, src->constBits());
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    auto* ef = QOpenGLContext::currentContext()->extraFunctions();
    if (!ef) return nullptr;
    GLsync fence = ef->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    return fence;
}
```

- [ ] **Step 2: 修改 doPreRender() — 局部上传 vs 全帧上传**

替换 `GLTextureViewport.cpp` 的 `doPreRender()` 中的上传部分（约 262-273 行）：

```cpp
    if (idx >= 0 && frame && !frame->image.isNull()) {
        makeCurrent();

        // 分辨率变化 → 重新分配纹理
        if (frame->remoteSize != m_ringBuffer.textureSize()) {
            m_ringBuffer.reallocate(frame->remoteSize);
        }

        if (frame->isFullFrame || !m_ringBuffer.textureSize().isValid()) {
            // 全帧：完整 glTexImage2D（重新分配纹理存储）
            int slotIdx = m_ringBuffer.acquireWriteSlot(frame->frameId);
            if (slotIdx >= 0) {
                GLsync fence = m_ringBuffer.uploadToSlot(slotIdx, frame->image);
                if (fence) {
                    m_ringBuffer.submitSlot(slotIdx, fence);
                } else {
                    m_ringBuffer.cancelSlot(slotIdx);
                }
            }
        } else {
            // 区域更新：glTexSubImage2D（需要已有槽位的纹理 ID）
            int slotIdx = m_ringBuffer.acquireWriteSlot(frame->frameId);
            if (slotIdx >= 0) {
                // 先确保基础纹理存在（从 compositor buffer 全帧初始化）
                if (m_ringBuffer.textureId(slotIdx) == 0) {
                    GLsync fence = m_ringBuffer.uploadToSlot(slotIdx, frame->image);
                    if (fence) {
                        m_ringBuffer.submitSlot(slotIdx, fence);
                    } else {
                        m_ringBuffer.cancelSlot(slotIdx);
                    }
                } else {
                    GLsync fence = m_ringBuffer.uploadSubImage(slotIdx,
                        frame->image, frame->dirtyRect);
                    if (fence) {
                        m_ringBuffer.submitSlot(slotIdx, fence);
                    } else {
                        m_ringBuffer.cancelSlot(slotIdx);
                    }
                }
            }
        }
        doneCurrent();
    }
```

- [ ] **Step 3: TextureRingBuffer 添加 reallocate() 方法**

在 `TextureRingBuffer.h` 新增：

```cpp
    /// 重建所有纹理槽位（分辨率变化时调用）
    void reallocate(const QSize& newSize);
```

在 `TextureRingBuffer.cpp` 实现：

```cpp
void TextureRingBuffer::reallocate(const QSize& newSize) {
    if (!ensureGLInitialized()) return;

    // 删除所有旧纹理
    for (int i = 0; i < kSlotCount; ++i) {
        if (m_slots[i].textureId != 0) {
            glDeleteTextures(1, &m_slots[i].textureId);
            m_slots[i].textureId = 0;
        }
        m_slots[i].size = QSize();
        m_slots[i].state.store(SlotState::Free, std::memory_order_release);
    }
    m_textureSize = newSize;
}
```

- [ ] **Step 4: 构建验证**

```bash
cmake --build build --config Debug
```
预期: 编译成功。

- [ ] **Step 5: 提交**

```bash
git add src/client/window/GLTextureViewport.cpp src/client/window/TextureRingBuffer.h src/client/window/TextureRingBuffer.cpp
git commit -m "feat: GLTextureViewport 局部 glTexSubImage2D + TextureRingBuffer 子区域上传

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

### Task 11: SessionManager 适配（格式检测 + 头校验移除）

**文件:**
- 修改: `src/client/managers/SessionManager.cpp`

- [ ] **Step 1: 移除 JPEG 硬编码头校验**

替换 `handleScreenData()` 中的 JPEG 头部校验（约 234-243 行）：

```cpp
// 将硬编码的 JPEG 头校验替换为通用图像数据校验：
// OLD:
//     if (screenData.imageData.size() >= 2) {
//         unsigned char byte0 = static_cast<unsigned char>(screenData.imageData[0]);
//         unsigned char byte1 = static_cast<unsigned char>(screenData.imageData[1]);
//         if (byte0 != 0xFF || byte1 != 0xD8) { ... }
//     }
//
// NEW:
    // 通用图像数据校验：非移动区域帧必须至少 2 字节
    const bool isMoveRect = screenData.flags &
        static_cast<quint8>(ScreenDataFlags::MOVE_RECT);
    if (!isMoveRect && screenData.imageData.size() < 2) {
        qCWarning(lcClient) << "SessionManager::handleScreenData() - Image data too small:"
                           << screenData.imageData.size();
        return;
    }
```

- [ ] **Step 2: 更新 FULL_FRAME 尺寸追踪**

在 `handleScreenData()` 中，在现有的 SCALED 检查后新增 FULL_FRAME 尺寸追踪：

```cpp
    // 全帧 → 更新 remoteScreenSize
    if (screenData.flags & static_cast<quint8>(ScreenDataFlags::FULL_FRAME)) {
        m_remoteScreenSize = QSize(screenData.originalWidth, screenData.originalHeight);
    }
```

- [ ] **Step 3: 构建验证 + 提交**

```bash
cmake --build build --config Debug
git add src/client/managers/SessionManager.cpp
git commit -m "fix: SessionManager 移除 JPEG 硬编码头校验，适配多格式

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase 5: 构建验证 & 测试

### Task 12: 完整构建 + 现有测试回归

- [ ] **Step 1: 完整 Debug 构建**

```bash
cmake --build build --config Debug
```
预期: 编译成功，0 errors, 0 warnings。

- [ ] **Step 2: 运行现有单元测试**

```bash
cd build && ctest --output-on-failure -L unit
```
预期: 所有现有单元测试通过。

- [ ] **Step 3: 运行核心集成测试**

```bash
cd build && ctest --output-on-failure -R "test_data_consistency|test_screen_data_flow|test_dataprocessing"
```
预期: 数据流和协议测试通过。

- [ ] **Step 4: 提交最终检查**

```bash
git status
git diff --stat HEAD~11
```
确认所有变更文件与设计文档第 8 节一致。

- [ ] **Step 5: 最终提交（如有遗漏文件）**

```bash
git add -u
git commit -m "chore: 构建验证 + 现有测试回归通过

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## 文件变更清单汇总

| 文件 | 任务 | 变动类型 |
|------|------|---------|
| `src/common/core/config/Constants.h` | Task 1 | 修改：编码常量 |
| `src/common/core/network/Protocol.h` | Task 2 | 修改：ScreenDataFlags |
| `src/server/dataflow/DataFlowStructures.h` | Task 3, 7 | 修改：ProcessedData + CapturedFrame 字段 |
| `src/client/core/DecodedFrame.h` | Task 4 | 修改：脏矩形字段 |
| `src/server/capture/DxgiCapture.h` | Task 5 | 修改：CaptureResult + DirtyRect 结构体 |
| `src/server/capture/DxgiCapture.cpp` | Task 5 | 修改：脏矩形提取 + 合并算法 |
| `src/server/dataprocessing/DataProcessingWorker.cpp` | Task 6, 7 | 修改：WebP 编码 + 区域路径 |
| `src/server/capture/ScreenCaptureWorker.h` | Task 7 | 修改：入队方法声明 |
| `src/server/capture/ScreenCaptureWorker.cpp` | Task 7 | 修改：区域/全帧/移动入队 |
| `src/server/clienthandler/ClientHandlerWorker.cpp` | Task 8 | 修改：FULL_FRAME/MOVE_RECT 标志 |
| `src/client/managers/DecodeWorker.h` | Task 9 | 修改：CompositorBuffer 成员 |
| `src/client/managers/DecodeWorker.cpp` | Task 9 | 修改：区域合成 + 移动 memcpy |
| `src/client/window/GLTextureViewport.cpp` | Task 10 | 修改：局部上传 |
| `src/client/window/TextureRingBuffer.h` | Task 10 | 修改：subImage + reallocate |
| `src/client/window/TextureRingBuffer.cpp` | Task 10 | 修改：局部上传实现 |
| `src/client/managers/SessionManager.cpp` | Task 11 | 修改：移除硬编码校验 |
