# WebP 编码 + 脏矩形区域更新 优化设计

## 概述

将客户端渲染管线中的帧编码与传输策略从"全帧 JPEG 软件编码"切换为"WebP 编码 + DXGI 脏矩形区域更新"，解决 1080p 下 Qt 内置 JPEG 编码器耗时 ~200ms/帧导致整个管线堵塞的致命瓶颈。

**三个核心改动**：
1. **WebP 替代 JPEG** — Qt 6 内置，跨平台零依赖，1080p 编码 ~15-25ms
2. **DXGI 脏矩形检测** — 仅编码屏幕变化区域，日常场景数据量减少 90%+
3. **客户端区域合成** — 维护全帧 CompositorBuffer，脏区域 glTexSubImage2D 局部上传

## 背景与数据

### 当前问题（基于 2026-06-08 日志分析）

| 指标 | 数值 |
|------|------|
| JPEG 编码耗时（1080p, 质量85） | **173-233ms/帧** |
| 60fps 单帧预算 | 16.7ms |
| 编码速度缺口 | **慢 12 倍** |
| 端到端延迟（捕获→客户端收到） | **2846ms** |
| JPEG 输出大小 | ~330KB/帧 |
| 客户端丢帧率 | 近乎 100%（"queue full or stopped" 刷屏） |
| 服务端捕获队列 | 938 帧后满，后续大量丢弃 |

### 瓶颈分析

Qt 内置 JPEG 编码器为纯软件实现，无 SIMD 加速。1080p BGRA 全帧（8MB 原始像素）→ JPEG 质量 85 编码耗时 ~200ms，是整个管线唯一瓶颈。GL 渲染（~0.1ms）、PBO 上传（~2ms）、TCP 发送（~5ms）均不是问题。

## 设计详情

### 1. 编码格式切换：JPEG → WebP

**范围**：`DataProcessingWorker::encodeImage()`

```
当前:
  QImage → convertToFormat(RGB32)
        → QImage::save(&buffer, "JPG", 85)
  耗时：~200ms/1080p，~330KB

优化后:
  QImage → QImage::save(&buffer, "WEBP", 95)
  耗时：~15-25ms/1080p，~250-350KB（质量 95）
```

**设计决策**：
- **质量参数 95**（近无损），覆盖远程桌面的文字、图标等对清晰度要求高的内容
- **跳过 `convertToFormat`**：WebP 编码器内部处理像素格式转换，避免额外全帧 memcpy
- **客户端解码零改动**：`QImage::loadFromData()` 自动识别 WebP 格式
- **编码失败回退**：WebP 编码失败时回退 JPEG（质量 85），确保不丢帧

**依赖**：
- Qt 6 内置 WebP 插件（libwebp），全平台可用
- 零额外第三方库依赖
- 若 Qt 安装时未包含 WebP 插件（极少见），CMake 构建时检测并告警

**常量变更**（`Constants.h`）：
```cpp
// 旧
constexpr int DEFAULT_JPEG_QUALITY = 85;

// 新
constexpr int DEFAULT_WEBP_QUALITY = 95;
```

### 2. DXGI 脏矩形检测（服务端）

**范围**：`DxgiCapture` → `ScreenCaptureWorker` → `DataProcessingWorker`

DXGI Desktop Duplication API 提供 `GetFrameDirtyRects()` 和 `GetFrameMoveRects()`，在 GPU 端（DWM 层）追踪屏幕变化，无 CPU 开销。

#### 2.1 DxgiCapture 新增接口

```cpp
// DxgiCapture.h 新增
struct DirtyRect {
    QRect rect;
    bool   isMoveRect = false;  // 移动区域（内容平移，不需编码）
    QPoint moveSrc;              // 移动源坐标（仅 isMoveRect=true 时有效）
};

struct CaptureResult {
    QImage              fullImage;       // 全帧图像（全帧模式）
    QVector<DirtyRect>  dirtyRects;      // 脏/移动矩形列表
    bool                isFullFrame;     // 是否全帧（首帧/分辨率切换/大面积变化）
    QSize               desktopSize;
    quint64             frameId;
};

CaptureResult captureFrameWithDirtyRects(int timeoutMs = 100);
```

**实现逻辑**：
1. 调用 `AcquireNextFrame()` + `GetFrameDirtyRects()` + `GetFrameMoveRects()`
2. 合并重叠与相邻矩形（间距 <16px 合并为条带，<16px 条带合并为块）
3. 脏矩形总数 >50 → 强制合并到 ≤16 个
4. 脏矩形总面积 >全屏 70% → 回退全帧编码
5. 仅从 GPU 纹理裁剪脏区域像素 → 小块 QImage（每区独立）
6. 移动区域不提取像素，仅记录 (src, dst, size)

#### 2.2 矩形合并算法

```
输入：DXGI 原始脏矩形 + 移动矩形（可能数十个碎片）
输出：合并后 ≤16 个矩形

算法：
1. 水平合并：同一行上间距 <16px 的矩形合并为条带
2. 垂直合并：Y 方向间距 <16px 的条带合并为块
3. 数量超限：扩大合并阈值翻倍，直到 ≤16 个
4. 过滤：丢弃完全被移动区域覆盖的脏矩形（无需编码）
```

#### 2.3 ScreenCaptureWorker 适配

```cpp
void ScreenCaptureWorker::performCapture() {
    // ...
    CaptureResult result = m_dxgiCapture->captureFrameWithDirtyRects(timeout);

    if (result.isFullFrame) {
        // 全帧路径（首帧/分辨率切换/大面积变化）
        enqueueFullFrame(result.fullImage);
    } else {
        // 区域路径
        for (const auto& dr : result.dirtyRects) {
            if (dr.isMoveRect) {
                enqueueMoveRect(dr);
            } else {
                enqueueDirtyRegion(result.fullImage.copy(dr.rect), dr.rect);
            }
        }
    }
}
```

#### 2.4 DataProcessingWorker 适配

- 全帧：WebP 编码整图 → 入队 ProcessedData（标记 fullFrame=true）
- 脏区域：WebP 编码区部图像 → 入队 ProcessedData（含 dirtyRect 坐标）
- **小区域（≤64×64 像素）使用 PNG**：Qt 内置 PNG 编码器对小图极快，且无损保证文字/图标清晰
- 移动区域：不入编码队列，直接构造 MoveRect 消息 → 入队 ProcessedData

### 3. 网络协议适配

**ScreenData 现有字段已支持**（`Protocol.h:206-222`）：
```cpp
struct ScreenData {
    quint16 x, y;           // 区域左上角坐标
    quint16 width, height;  // 区域尺寸
    // ...
};
```

全帧模式下 `x=0, y=0, width=1920, height=1080`，区域模式下为具体脏矩形坐标。协议无需字段扩展。

**ScreenDataFlags 新增**：
```cpp
enum class ScreenDataFlags : quint8 {
    NONE        = 0x00,
    SCALED      = 0x01,  // 现有
    FULL_FRAME  = 0x02,  // 新增：标记为全帧（客户端据此初始化 compositor buffer）
    MOVE_RECT   = 0x04,  // 新增：标记为移动区域
};
```

**MoveRect 编码**（嵌入 ScreenData.imageData 前 20 字节）：
```
[srcX:4] [srcY:4] [dstX:4] [dstY:4] [moveW:2] [moveH:2] = 20 字节
客户端收到后直接 memcpy，跳过编解码。
```

### 4. 客户端区域合成

**范围**：`DecodeWorker` → `DecodedFrame` → `GLTextureViewport`

#### 4.1 DecodedFrame 扩展

```cpp
struct DecodedFrame {
    QImage   image;         // 解码后图像（COW 引用 compositor buffer 或全帧）
    QSize    remoteSize;    // 远端桌面总尺寸
    QRect    dirtyRect;     // 本帧脏区域（全帧模式下 = image.rect()）
    quint64  frameId = 0;
    bool     isFullFrame = false;
    bool     isMoveRect  = false;
    QPoint   moveSrc;       // 移动区域源坐标
    QPoint   moveDst;       // 移动区域目标坐标
    QSize    moveSize;      // 移动区域大小
};
```

#### 4.2 DecodeWorker CompositorBuffer

```cpp
class DecodeWorker {
    // 新增成员
    QImage m_compositorBuffer;  // 全尺寸远端桌面缓冲
    QMutex m_compositorMutex;   // 保护 compositor buffer 写入
};
```

**workLoop 区域合成逻辑**：
1. 解码 WebP/PNG → 局部 QImage
2. 若为全帧：替换 `m_compositorBuffer`
3. 若为脏区域：将解码图像粘贴到 `m_compositorBuffer` 对应坐标
4. 若为移动区域：`memcpy` 像素块 → 同时标记源和目标区域为 dirty
5. 输出到 TripleBuffer：COW 引用 `m_compositorBuffer` + 设置 `dirtyRect`

#### 4.3 GLTextureViewport 局部上传

**doPreRender 变化**：
```cpp
void GLTextureViewport::doPreRender() {
    DecodedFrame* frame = nullptr;
    int idx = m_inputBuffer.getReadSlot(frame);
    if (idx < 0) return;

    // 分辨率变化 → 重新分配纹理
    if (frame->remoteSize != m_ringBuffer.textureSize()) {
        m_ringBuffer.reallocate(frame->remoteSize);
    }

    if (frame->isFullFrame) {
        // 全帧：glTexImage2D（重新分配纹理存储）
        uploadFullFrame(frame);
    } else {
        // 区域更新：glTexSubImage2D（仅更新脏区域）
        uploadDirtyRegion(frame);
    }
}
```

**性能收益**：日常场景脏区域 <5% 屏幕面积，PBO memcpy 从 1-2ms 降到 <0.05ms。

### 5. 错误处理与边界情况

| 场景 | 处理策略 |
|------|---------|
| DXGI 脏矩形列表为空但帧有效 | 回退全帧编码 |
| 脏矩形数量 >50 且合并后仍 >16 | 回退全帧编码 |
| WebP 编码失败 | 回退 JPEG 编码（质量 85） |
| 客户端 CompositorBuffer 未初始化 | 跳过区域帧，等待下一全帧 |
| 远程分辨率变化 | 重新分配 compositor buffer + 纹理，下帧强转全帧 |
| 解码失败（损坏帧） | 跳过该帧，不影响 compositor buffer |
| 连接重连 | 发送全帧初始化客户端 buffer |

### 6. 线程模型（不变）

| 线程 | 组件 | 操作 |
|------|------|------|
| 捕获线程 | ScreenCaptureWorker | DXGI 捕获 + 脏矩形检测 → 入队 CQ |
| 编码线程池 | DataProcessingWorker (QThreadPool) | WebP/PNG 编码 → 入队 PQ |
| 网络线程 | ClientHandlerWorker | 从 PQ 取帧 → TCP 发送 |
| DecodeThread | DecodeWorker | 解码 + CompositorBuffer 合成 → TripleBuffer |
| GUI 线程 | GLTextureViewport | doPreRender 局部上传 + paintGL 纯绘制 |

### 7. 测试策略

| 层级 | 内容 |
|------|------|
| **单元测试** | 矩形合并算法（间距阈值、数量限制、大面积回退） |
| **集成测试** | 捕获→WebP编码→解码→合成的完整闭环 |
| **性能测试** | 1080p 全帧 WebP <25ms；脏区域（5%面积）<3ms；全帧回退路径 <30ms |
| **边界测试** | 零脏矩形、100个1px碎片、全屏变化、分辨率切换、随机噪声帧 |
| **回归测试** | 现有 test_screencapture, test_dataprocessing, test_data_consistency 全通过 |

### 8. 文件变更清单

| 文件 | 变动类型 | 说明 |
|------|---------|------|
| `src/server/capture/DxgiCapture.h` | 新增接口 | `CaptureResult` 结构体 + `captureFrameWithDirtyRects()` |
| `src/server/capture/DxgiCapture.cpp` | 新增实现 | 脏矩形提取、合并、区域裁剪 |
| `src/server/capture/ScreenCaptureWorker.h` | 修改 | 适配区域捕获流程 |
| `src/server/capture/ScreenCaptureWorker.cpp` | 修改 | `performCapture` 路径分支 |
| `src/server/dataprocessing/DataProcessingWorker.cpp` | 修改 | JPEG→WebP；区域/全帧/移动分支 |
| `src/server/dataflow/DataFlowStructures.h` | 修改 | `ProcessedData` 增加脏矩形/移动区域字段 |
| `src/common/core/network/Protocol.h` | 修改 | `ScreenDataFlags` 新增 FULL_FRAME / MOVE_RECT |
| `src/common/core/config/Constants.h` | 修改 | 编码格式和质量常量 |
| `src/client/core/DecodedFrame.h` | 修改 | 增加 dirtyRect / moveRect 字段 |
| `src/client/managers/DecodeWorker.h` | 修改 | 新增 CompositorBuffer 成员 |
| `src/client/managers/DecodeWorker.cpp` | 修改 | 区域合成逻辑 |
| `src/client/window/GLTextureViewport.cpp` | 修改 | `doPreRender` 支持局部上传 |
| `src/client/managers/SessionManager.cpp` | 修改 | `handleScreenData` 适配区域帧 |

## 风险评估

| 风险 | 严重度 | 缓解措施 |
|------|-------|---------|
| WebP 编码比预期慢 | 低 | 质量 95 已保守，实测可降至 90；回退 JPEG 保底 |
| DXGI 脏矩形不精确（某些 GPU 返回全帧脏区域） | 低 | 全帧模式本就 ≈ 当前行为，无退化 |
| CompositorBuffer COW 竞态 | 中 | QMutex 保护写入 + COW 保证读取安全 |
| 移动区域检测语义错误（错误位置的像素） | 中 | 移动后同时标记源和目标为 dirty 确保覆盖 |
| 小碎片过多导致网络包膨胀 | 低 | 矩形合并 + 数量上限 + 全帧回退三重保障 |
