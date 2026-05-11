# 远程桌面帧率优化方案

> 文档版本: 1.0  
> 日期: 2026-04-10  
> 状态: 3.3.3 已完成（2026-04-24 第一轮 + 2026-05-11 第二轮激进重构）

---

## 1. 当前架构概述

### 1.1 服务端数据管线（Producer-Consumer）

```
ScreenCaptureWorker → CaptureQueue(120) → DataProcessingWorker → ProcessedQueue(120) → ClientHandlerWorker
    [GDI grabWindow]                        [JPEG+zstd encode]                           [TCP send]
```

### 1.2 客户端渲染管线

```
TCP recv → SessionManager → screenImageQueue(3) → ClientManager(8ms poll) → RenderManager
           [zstd解压+JPEG解码+缩放+QPixmap转换]                              [QGraphicsView渲染]
```

### 1.3 当前帧率基线

| 分辨率 | 目标帧率 | 实测瓶颈耗时 | 实际可达帧率（估算） |
|--------|---------|-------------|-------------------|
| 1080p  | 30fps   | 捕获~20ms + 编码~15ms + 网络+客户端~10ms | ~22fps |
| 1440p  | 30fps   | 捕获~35ms + 编码~18ms + 网络+客户端~12ms | ~15fps |
| 4K     | 30fps   | 捕获~60ms + 编码~25ms + 网络+客户端~15ms | ~10fps |

---

## 2. 瓶颈分析

按严重程度从高到低排列：

### 🔴 瓶颈 #1：GDI 屏幕捕获（致命瓶颈）

**位置**: `src/server/capture/ScreenCaptureWorker.cpp` → `captureScreen()`  
**代码**: `m_primaryScreen->grabWindow(0)` (QScreen::grabWindow)  
**原因**: Qt 的 `grabWindow()` 底层使用 GDI `BitBlt`，每帧需要 CPU 从显存拷贝完整位图  
**耗时**: 1080p ~20ms, 4K ~60ms  
**帧率上限**: 1080p 仅 ~50fps，4K 仅 ~16fps（仅捕获环节）

**影响**: 这是整条管线的**绝对瓶颈**。即使其他所有环节零耗时，4K 分辨率下也无法达到 30fps。

### 🟡 瓶颈 #2：QImage 全帧内存拷贝

**位置**: `ScreenCaptureWorker::captureScreen()` 中 `QPixmap::toImage()` 转换  
**原因**: `grabWindow()` 返回 QPixmap（GPU 纹理），`toImage()` 生成 CPU 端 QImage 副本  
**耗时**: 1080p ~8MB/帧 (RGBA)，涉及 GPU→CPU readback + 内存分配  
**帧率影响**: 每帧额外 3-8ms

### 🟡 瓶颈 #3：JPEG 编码效率

**位置**: `src/server/dataprocessing/DataProcessingWorker.cpp` → `encodeImageParallel()`  
**代码**: `QImage::save(&buffer, "JPG", quality)`  
**原因**: Qt 内置 JPEG 编码器基于标准 libjpeg，未使用 SIMD 优化的 libjpeg-turbo  
**耗时**: 1080p ~10-20ms（取决于质量设置）  
**备注**: 当前已使用 `QtConcurrent::mapped` 做批量并行编码，部分缓解了此问题

### 🟠 瓶颈 #4：单帧发送模式

**位置**: `src/server/clienthandler/ClientHandlerWorker.cpp` → `sendScreenDataFromQueue()`  
**代码**: 每次 `processTask()` 仅出队并发送 1 帧（第265行注释："每次只发送一条数据，取消批处理"）  
**原因**: 加上 Worker 基类 workLoop 的 `QThread::msleep(1)` 间隔，发送频率被人为限制  
**影响**: 即使队列中有积压帧，发送速率也被限制在 ~500fps 理论上限（实际受网络 IO 约束远低于此）

### 🟠 瓶颈 #5：客户端三重格式转换

**位置**: `src/client/managers/SessionManager.cpp` → `handleScreenData()`  
**流程**:
```
JPEG bytes → loadFromData("JPEG") → QImage   [JPEG解码 ~10ms]
QImage → scaled(originalSize)                  [缩放恢复 ~5-10ms, 仅缩放帧]
QImage → QPixmap::fromImage()                  [CPU→GPU上传 ~3ms]
```
**问题**: `scaled()` 使用 `Qt::SmoothTransformation`（双线性插值），CPU 密集；且每帧都做 QPixmap 转换即使后续只用 QImage 入队

### 🟠 瓶颈 #6：RenderManager 每帧 fitInView

**位置**: `src/client/window/RenderManager.cpp` → `setRemoteScreen()`  
**代码**: 每帧调用 `m_view->fitInView(m_pixmapItem, Qt::KeepAspectRatio)`  
**原因**: `fitInView()` 重新计算变换矩阵并触发场景全量重绘，对于尺寸不变的连续帧完全冗余  
**影响**: 每帧额外 1-3ms + 不必要的 GPU 提交

### 🟢 瓶颈 #7：客户端队列过小

**位置**: `SessionManager.h` 中 `MAX_QUEUE_SIZE = 3`  
**问题**: 3 帧的缓冲在网络抖动时极易丢帧（`while` 循环丢弃旧帧）  
**影响**: 较低优先级，但会导致画面卡顿感

---

## 3. 优化方案

### 3.1 阶段一：Windows DXGI Desktop Duplication（收益最大）

**目标**: 捕获耗时从 20-60ms 降至 1-2ms  
**优先级**: ⭐⭐⭐⭐⭐  
**预估复杂度**: 高  
**预估收益**: 帧率提升 3-10 倍

#### 3.1.1 技术原理

DXGI Desktop Duplication API（Windows 8+）直接从 DWM 合成器获取桌面纹理，避免 GDI 的 CPU readback：
- GPU 端纹理复制，延迟 < 1ms
- 支持脏矩形（dirty rects），仅传输变化区域
- 支持鼠标形状/位置单独获取

#### 3.1.2 实现设计

**新增文件**:
```
src/server/capture/
├── ScreenCaptureWorker.h            # 不变，接口保持
├── ScreenCaptureWorker.cpp          # 修改：调用平台捕获器
├── PlatformCapture.h                # 新增：平台捕获抽象接口
├── DxgiCapture.h                    # 新增：DXGI 实现头文件
├── DxgiCapture.cpp                  # 新增：DXGI Desktop Duplication 实现
├── GdiCapture.h                     # 新增：GDI 回退实现（原 grabWindow 逻辑）
├── GdiCapture.cpp                   # 新增：GDI 实现
├── MacOSCapture.h                   # 新增：macOS 实现（CGWindowListCreateImage）
└── MacOSCapture.cpp                 # 新增：macOS 实现
```

**抽象接口**:
```cpp
// PlatformCapture.h
#pragma once
#include <QtGui/QImage>
#include <QtCore/QRect>
#include <vector>
#include <memory>

struct CaptureResult {
    QImage image;                         // Captured frame
    std::vector<QRect> dirtyRects;        // Changed regions (empty = full frame)
    bool isFullFrame = true;              // Whether this is a complete frame
    std::chrono::steady_clock::time_point timestamp;
};

class PlatformCapture {
public:
    virtual ~PlatformCapture() = default;
    
    virtual bool initialize(int screenIndex = 0) = 0;
    virtual void shutdown() = 0;
    
    virtual CaptureResult captureFrame() = 0;
    virtual bool supportsPartialCapture() const = 0;
    
    static std::unique_ptr<PlatformCapture> create();  // Factory
};
```

**DXGI 核心实现要点**:
```cpp
// DxgiCapture.cpp 核心流程
// 1. IDXGIOutputDuplication::AcquireNextFrame() — 获取新帧
// 2. ID3D11DeviceContext::CopyResource() — GPU 纹理复制
// 3. ID3D11Texture2D::Map() — 映射到 CPU 可读内存
// 4. 构造 QImage 直接引用映射内存（零拷贝）
// 5. ReleaseFrame()
```

**CMake 集成**:
```cmake
if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE
        src/server/capture/DxgiCapture.cpp
        src/server/capture/GdiCapture.cpp
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE d3d11 dxgi)
elseif(APPLE)
    target_sources(${PROJECT_NAME} PRIVATE
        src/server/capture/MacOSCapture.cpp
    )
endif()
```

#### 3.1.3 验收标准

- [ ] 1080p 捕获延迟 < 3ms（含 Map/Unmap）
- [ ] 4K 捕获延迟 < 5ms
- [ ] DXGI 初始化失败时自动回退到 GDI
- [ ] 支持多显示器选择
- [ ] 脏矩形信息能传递到后续管线

---

### 3.2 阶段二：零拷贝帧传递（中等收益）

**目标**: 消除管线中的冗余内存拷贝  
**优先级**: ⭐⭐⭐⭐  
**预估复杂度**: 中  
**预估收益**: 每帧减少 5-15ms

#### 3.2.1 改造 CapturedFrame 为共享指针

**修改文件**: `src/server/dataflow/DataFlowStructures.h`

当前 `CapturedFrame` 包含 `QImage image` 值成员，队列入队/出队产生深拷贝。改造为：
```cpp
struct CapturedFrame {
    std::shared_ptr<QImage> image;          // Shared ownership, no deep copy
    quint64 frameId = 0;
    qint64  captureTimestamp = 0;
    QSize   originalSize;
    // ... other metadata
};
```

#### 3.2.2 消除 QPixmap↔QImage 转换

**修改文件**: `src/server/capture/ScreenCaptureWorker.cpp`

当前流程: `grabWindow() → QPixmap → toImage() → QImage`  
优化后: DXGI 直接输出 QImage（从映射的 GPU 内存构造），跳过 QPixmap 阶段

#### 3.2.3 客户端侧：移除不必要的 QPixmap 缓存

**修改文件**: `src/client/managers/SessionManager.cpp`

当前第332行:
```cpp
m_currentScreen = QPixmap::fromImage(image);  // 仅用于内部缓存，实际入队的是 QImage
```
这个 `QPixmap::fromImage()` 转换完全多余（入队的是 QImage，渲染也用 QImage），应删除。

#### 3.2.4 验收标准

- [ ] CapturedFrame 入队/出队不触发 QImage 深拷贝
- [ ] ScreenCaptureWorker 不再调用 toImage()
- [ ] SessionManager 不再维护 m_currentScreen QPixmap 缓存

---

### 3.3 阶段三：客户端渲染优化（中等收益）

**目标**: 客户端每帧处理耗时从 ~20ms 降至 ~5ms  
**优先级**: ⭐⭐⭐⭐  
**预估复杂度**: 中  
**预估收益**: 客户端帧处理提速 3-4 倍

#### 3.3.1 消除冗余的 scaled() 恢复

**修改文件**: `src/client/managers/SessionManager.cpp` (第320-329行)

**问题**: 服务端缩放（0.75x/0.5x）后客户端用 `Qt::SmoothTransformation` 恢复原始尺寸。这是一个**无意义的操作**——缩放已丢失的信息无法恢复，且 SmoothTransformation 耗时 5-10ms。

**方案**: 客户端直接使用缩放后的图像尺寸渲染，由 RenderManager 的 `fitInView()` 负责显示缩放。

```cpp
// 优化前
if (screenData.flags & SCALED) {
    image = image.scaled(originalSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// 优化后：完全删除 scaled()，仅记录原始尺寸供布局使用
if (screenData.flags & SCALED) {
    // Store original size for aspect ratio, but do NOT upscale the image
    m_remoteScreenSize = QSize(screenData.originalWidth, screenData.originalHeight);
} else {
    m_remoteScreenSize = image.size();
}
```

#### 3.3.2 RenderManager 条件性 fitInView

**修改文件**: `src/client/window/RenderManager.cpp`

**优化**: 仅在窗口大小变化或远程屏幕分辨率变化时调用 `fitInView()`，连续同尺寸帧只更新 pixmap 数据：

```cpp
void RenderManager::setRemoteScreen(const QImage& image) {
    QPixmap pixmap = QPixmap::fromImage(image);
    m_pixmapItem->setPixmap(pixmap);
    
    // Only recalculate view transform when dimensions change
    if (pixmap.size() != m_lastPixmapSize) {
        m_lastPixmapSize = pixmap.size();
        m_view->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    }
}
```

#### 3.3.3 考虑 OpenGL 纹理直传（进阶）

当前 `QGraphicsView` 每帧做 CPU→GPU 纹理上传。若使用 `QOpenGLWidget` 作为 viewport + 直接 `glTexSubImage2D` 更新纹理，可避免 Qt 的中间层开销。

**此项为进阶优化，建议在 3.3.1 和 3.3.2 完成后评估是否仍需要。**

实施状态：已完成。实际落地位于 `src/client/window/GLTextureViewport.{h,cpp}` + `src/common/core/config/RenderConfig.h`：
- OpenGL 纹理直传 (commit `20430d2`)：GLTextureViewport 替代 QGraphicsView 渲染路径
- RGBA8888 零拷贝分发 (commit `5fafda3`)：按 QImage::Format 匹配 GL 格式
- PBO 双缓冲异步上传 + 脏帧门控 (commit `79b593a`)
- Latest-wins 帧队列 + VSync 可配置 (commit `11f9395`, `5106a42`)
- 端到玻璃延迟度量 (commit `6f05f23`)：lcRefreshMetrics 日志分类

详细迭代过程与度量方法见 `docs/superpowers/plans/2026-04-24-client-refresh-rate-optimization.md`。

第二轮激进重构 (2026-05-11，commit `47ceb8c` ~ `9b175df`)：
- 移除 QGraphicsView 回退路径，GLTextureViewport 成为唯一渲染表面 (commit `b4378a8`)
- Triple-Buffered 无锁跨线程帧交换替代 QQueue+信号 (commit `47ceb8c`)
- Worker 线程通过共享 GL 上下文直写 GPU 纹理 (commit `f64d2cf`)
- Persistent Mapped PBO (GL_ARB_buffer_storage) 消除每帧 alloc/map 开销 (commit `9b175df`)
详细迭代过程见 `docs/superpowers/plans/2026-05-07-client-render-radical-refactor.md`。

#### 3.3.4 验收标准

- [ ] 客户端不再对收到的帧做 `scaled()` 恢复
- [ ] 连续同尺寸帧不触发 `fitInView()`
- [ ] 主观画面流畅度无明显降低

---

### 3.4 阶段四：发送管线优化（低-中收益）

**目标**: 提升服务端→客户端的数据吞吐率  
**优先级**: ⭐⭐⭐  
**预估复杂度**: 低  
**预估收益**: 有效发送帧率提升 20-50%

#### 3.4.1 批量发送

**修改文件**: `src/server/clienthandler/ClientHandlerWorker.cpp`

每次 `processTask()` 出队并发送多帧（例如最多 3 帧），减少 workLoop 中 `msleep(1)` 的累积开销：

```cpp
void ClientHandlerWorker::sendScreenDataFromQueue() {
    constexpr int MAX_BATCH_SIZE = 3;
    int sent = 0;
    
    while (sent < MAX_BATCH_SIZE) {
        ProcessedData processedData;
        if (!m_queueManager->dequeueProcessedData(processedData)) break;
        if (!processedData.isValid()) continue;
        
        // Build and send ScreenData message...
        sendScreenDataMessage(processedData);
        sent++;
    }
}
```

#### 3.4.2 Worker workLoop 自适应休眠

**修改文件**: `src/common/core/threading/Worker.cpp` (workLoop, 第238行附近)

当前 workLoop 在**每次** `processTask()` 后固定 `QThread::msleep(1)`。对于高帧率场景，这浪费了宝贵的 1ms。

**方案**: 让 `processTask()` 返回是否有实际工作，有工作时不休眠：

```cpp
// Worker.h: 修改 processTask 签名（或新增 hasWork 虚函数）
virtual bool processTask() = 0;  // return true if work was done

// Worker.cpp workLoop:
while (!shouldStop()) {
    processEvents();
    waitIfPaused();
    bool didWork = processTask();
    if (!didWork) {
        QThread::msleep(1);  // Only sleep when idle
    }
}
```

**注意**: 这是 Worker 基类修改，影响所有 Worker 子类，需要全面测试。

#### 3.4.3 验收标准

- [ ] ProcessedQueue 积压时发送速率明显提升
- [ ] Worker 空闲时 CPU 占用不上升（仍有 sleep）
- [ ] 所有现有 Worker 子类编译通过并测试通过

---

### 3.5 阶段五：客户端队列与轮询优化（低收益）

**目标**: 减少丢帧和轮询开销  
**优先级**: ⭐⭐  
**预估复杂度**: 低  
**预估收益**: 减少卡顿感，降低 CPU 空转

#### 3.5.1 增大客户端帧队列

**修改文件**: `src/client/managers/SessionManager.h`

```cpp
static constexpr int MAX_QUEUE_SIZE = 5;  // 从 3 增大到 5
```

更大的缓冲可以吸收网络抖动，但不宜过大（增加延迟）。

#### 3.5.2 事件驱动替代定时轮询

**修改文件**: `src/client/ClientManager.cpp`

当前 `m_screenUpdateTimer` 以 8ms 间隔轮询所有 SessionManager 的队列。改为条件变量/信号通知：

```cpp
// SessionManager: 入队后发射轻量信号
emit frameAvailable();  // 无参数，仅作通知

// ClientManager: 连接信号而非定时器
connect(session, &SessionManager::frameAvailable,
        this, &ClientManager::onFrameAvailable, Qt::QueuedConnection);
```

#### 3.5.3 验收标准

- [ ] 无帧可渲染时 ClientManager 不消耗 CPU
- [ ] 帧可用时渲染延迟 < 2ms（信号传递开销）

---

## 4. 实施路线图

```
阶段     优先级  复杂度   预估耗时   依赖
───────────────────────────────────────────────
阶段一   最高    高       3-5天     无
阶段二   高      中       1-2天     阶段一（部分）
阶段三   高      中       1天       无
阶段四   中      低       1天       无
阶段五   低      低       0.5天     无
```

**建议实施顺序**: 阶段一 → 阶段三 → 阶段二 → 阶段四 → 阶段五

- 阶段一和阶段三可并行开发（分属服务端和客户端）
- 阶段二依赖阶段一的 CaptureResult 结构设计
- 阶段四和阶段五独立性强，可在任意时机插入

---

## 5. 预期效果

### 优化前（当前状态）

| 分辨率 | 捕获 | 编码 | 发送 | 客户端 | 总计 | 有效帧率 |
|--------|------|------|------|--------|------|---------|
| 1080p  | 20ms | 15ms | 2ms  | 18ms   | ~55ms | ~18fps |
| 4K     | 60ms | 25ms | 5ms  | 25ms   | ~115ms | ~9fps |

### 优化后（预估）

| 分辨率 | 捕获(DXGI) | 编码 | 发送(批量) | 客户端(优化) | 总计 | 有效帧率 |
|--------|-----------|------|-----------|-------------|------|---------|
| 1080p  | 2ms       | 12ms | 1ms       | 5ms         | ~20ms | ~50fps |
| 4K     | 3ms       | 20ms | 2ms       | 8ms         | ~33ms | ~30fps |

**关键指标提升**:
- 1080p: 18fps → 50fps（约 2.8 倍提升）
- 4K: 9fps → 30fps（约 3.3 倍提升）
- 端到端延迟: ~55ms → ~20ms（1080p），~115ms → ~33ms（4K）

---

## 6. 风险与注意事项

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| DXGI 初始化失败（旧GPU/RDP会话） | 捕获失败 | 自动回退 GDI 捕获 |
| Worker::processTask 签名变更 | 所有子类需修改 | 可用默认返回值保持兼容 |
| 删除 scaled() 后画面模糊 | 用户感知 | fitInView 自动缩放弥补 |
| 批量发送导致网络突发 | 丢包/延迟 | 限制批量大小，配合 Nagle 禁用 |
| 客户端信号驱动可能消息风暴 | UI 线程阻塞 | 使用 coalescing 合并通知 |

---

## 7. 测试计划

每个阶段完成后需通过以下测试：

### 单元测试
- `test_platform_capture` — DXGI/GDI 捕获正确性
- `test_captured_frame` — shared_ptr 零拷贝验证
- `test_render_manager` — fitInView 条件触发
- `test_batch_send` — 批量发送正确性

### 集成测试
- 完整管线端到端帧率测试（1080p / 4K）
- DXGI 回退到 GDI 测试
- 网络抖动下的丢帧率测试

### 性能基准测试
- 各阶段捕获延迟对比（msleep 精确计时）
- 内存占用对比（零拷贝前后）
- CPU 占用对比（客户端渲染优化前后）
