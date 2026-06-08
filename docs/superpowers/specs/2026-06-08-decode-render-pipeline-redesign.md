# 解码→渲染管线重构设计

## 概述

将客户端接收队列到 GL 绘制的中间管线完全重构。核心变化：

1. **DecodeWorker 回归纯 CPU**：不再触碰 GL，只做 JPEG 解码
2. **GL 操作回到 GUI 线程**：纹理上传、fence 管理、PBO 操作均在 GUI 线程的原生 GL 上下文中完成
3. **PreRender 独立于 paintGL**：帧上传由 `frameDecoded` 信号驱动，paintGL 回到纯渲染职责
4. **纹理环缓冲区**：替代旧的三件套（TripleBuffer<FrameSlot> + 双缓冲纹理 + predecode/renderingDone 信号）

## 数据流

```
SessionManager             DecodeWorker               GUI 线程
(网络线程)                 (纯 CPU, DecodeThread)     (所有 GL 操作)

  enqueueFrame()             │                           │
  ThreadSafeQueue ──────────→│                           │
                             │  JPEG 解码 → QImage      │
                             │                           │
                             │  TripleBuffer ───────────→│
                             │  <DecodedFrame>           │
                             │  emit frameDecoded()      │
                             │                           │
                             │                     ┌─── doPreRender()
                             │                     │    从 TripleBuffer 取帧
                             │                     │    QImage → PBO → 纹理
                             │                     │    fence → InFlight
                             │                     │
                             │                     ├─── pollFences()
                             │                     │    InFlight → Ready
                             │                     │    Ready → update()
                             │                     │
                             │                     └─── paintGL()
                             │                          纯绘制
                             │                          getReadySlot → bind → draw
                             │                          releaseSlot (Ready → Free)
```

## 组件设计

### 1. DecodeWorker（极简重写）

`DecodeTask` 内部结构保留不变：

```cpp
struct DecodeTask {
    ScreenData screenData;
    QSize      remoteSize;
};
```

**文件**：`src/client/managers/DecodeWorker.h/.cpp`

```cpp
class DecodeWorker : public QObject {
    Q_OBJECT
public:
    struct DecodedFrame {
        QImage   image;
        QSize    remoteSize;
        quint64  frameId = 0;
    };

    bool enqueueFrame(ScreenData data, const QSize& remoteSize);
    void setOutputBuffer(TripleBuffer<DecodedFrame>* buf);
    void requestStop();
    bool isRunning() const { return m_running.load(); }

signals:
    void decodeError(const QString& message);
    void stopped();
    void frameDecoded(quint64 frameId);

public slots:
    void start();

private slots:
    void workLoop();

private:
    ThreadSafeQueue<DecodeTask> m_queue{4};
    TripleBuffer<DecodedFrame>* m_outputBuffer = nullptr;
    QImage m_decodeBuffer;
    std::atomic<quint64> m_nextFrameId{1};
    std::atomic<bool> m_running{false};
};
```

**workLoop 最简逻辑**：

1. 队列空 → `QThread::msleep(1)` → `continue`
2. 出队 → JPEG 解码 → 输出到 TripleBuffer（`std::move` 零拷贝）
3. TripleBuffer 满 → 丢弃帧
4. 解码成功 → `emit frameDecoded(frameId)`

**砍掉的旧内容**：
- `m_glContext`, `m_glSurface`, `m_glUploadReady`, `m_glViewport` — 全部 GL 相关
- `m_frameBuffer`（TripleBuffer<FrameSlot>*）→ 改为 `m_outputBuffer`（TripleBuffer<DecodedFrame>*）
- `m_frameId` → `m_nextFrameId`
- `m_predecodeSlot` — 预解码槽
- `predecodeSignal()`, `renderingDoneSignal()` — 旧的同步信号查询
- `initializeGL()`, `cleanupGL()`, `moveGLToThread()`, `setGLViewport()`
- `processFrameDecodeAndUpload()`, `processOneFrame()` — 旧的复杂管线

### 2. TextureRingBuffer（新增类，GLTextureViewport 内部持有）

**文件**：`src/client/window/GLTextureViewport.h`（独立类或嵌套于 GLTextureViewport）

#### 槽位状态机

```
Free ──→ acquireWriteSlot() ──→ InFlight
                                    │
                                    │ pollFences() signaled
                                    ↓
Ready ←─────────────────────────────┘
  │
  │ releaseSlot()
  ↓
Free
```

#### 每个槽位

```cpp
struct Slot {
    GLuint                textureId = 0;    // 所属纹理对象
    GLsync                fence     = nullptr;
    QSize                 size;
    std::atomic<SlotState> state{SlotState::Free};
    quint64               frameId   = 0;   // 全局帧序号，保证绘制顺序
};
```

#### 帧顺序保证

- Worker 分配单调递增的 `frameId`
- `getReadySlot(expectedFrameId)` 只返回 state==Ready 且 frameId==expected 的槽位
- 即使多个槽位乱序变 Ready，paintGL 按 frameId 严格顺序消费

#### deltaReset

如果 `expectedFrameId` 的槽位长期未 Ready 但更新的帧已 Ready：
- paintGL 跳过旧帧，直接跳到 `newestReadyFrameId()`
- 远程桌面场景下实时性优先于完整性

#### PBO 策略

- 单一持久 PBO 路径（`GL_ARB_buffer_storage`）
- 不可用时 fallback 直传（`glTexSubImage2D` CPU 指针）
- 无传统 PBO 中间路径
- PBO 双缓冲：`m_pbo[2]` + `m_pboPtr[2]`
- PBO 不属于任何槽位，是 uploadToSlot 的临时中转

### 3. GLTextureViewport 变化

#### 保留

- `m_shaderProgram`, `m_vao`, `m_vertexBuffer` — 着色器/几何
- `m_renderRect`, `m_textureSize` — 渲染区域
- `m_glCleanedUp` — 生命周期标记
- `m_vsyncEnabled`, `m_pollTimer` — VSync 控制
- `glContextReady` 信号

#### 新增

| 成员 | 说明 |
|------|------|
| `TripleBuffer<DecodedFrame> m_inputBuffer` | 接收解码帧 |
| `TextureRingBuffer m_ringBuffer` | 纹理环 |
| `QTimer* m_fallbackTimer` | 保底定时器（16ms，连接建立后启动，断开时停止） |
| `quint64 m_lastRenderedFrameId{0}` | 已渲染最后帧序号 |

#### 新增方法

| 方法 | 驱动源 | 职责 |
|------|--------|------|
| `doPreRender()` | `frameDecoded` 信号（QueuedConnection） | 从 TripleBuffer 取帧 → 获取空闲槽位 → PBO → 纹理 → fence → InFlight → 末尾调用 pollFences() |
| `onFallbackTimer()` | `m_fallbackTimer` 超时 | 末尾调用 pollFences()，确保无新帧时最后一帧的 fence 被收割 |
| `pollFences()` | 仅在 `doPreRender` 或 `onFallbackTimer` 末尾被调用 | InFlight → Ready，Ready 时 `QMetaObject::invokeMethod(this, "update", ...)` |
| `inputBuffer()` | 外部 | 返回 `&m_inputBuffer` |

#### 删除

| 删除项 | 原因 |
|--------|------|
| `m_textureId[2]`, `m_displayTexIndex` | 双缓冲纹理 → 纹理环 3 槽 |
| `m_pbo[]`, `m_persistentPbo*`, `m_sharedPboIndex` | PBO 移入 TextureRingBuffer |
| `m_frameBuffer`, `m_consumedSlot` | TripleBuffer<FrameSlot> → TripleBuffer<DecodedFrame> |
| `m_predecodeSignal`, `m_renderingDoneSignal` | 同步信号废弃 |
| `m_needsRepaint`, `m_consecutiveSkips` | 旧背压机制废弃 |
| `m_pendingArrivalTs`, `m_metrics*` | 延迟度量可选重建 |
| `uploadFromWorker()`, `requestRepaint()`, `CheckForNewFrameAfterPaint()` | 旧 API |
| `attachFrameBuffer()`, `setRemoteSize()`, `mapToRemote()`, `mapFromRemote()` | 坐标映射已由 RenderManager 接管 |

#### paintGL 新逻辑

1. `getReadySlot(m_lastRenderedFrameId + 1)` — 找期望帧
2. 无 → deltaReset 检查 → 无可用帧 → 直接 return
3. glClear → viewport → bind texture → glDrawArrays
4. releaseSlot —— Ready → Free

paintGL 不再做任何上传、fence 等待或同步操作。

### 4. 清理生命周期

#### 正常路径

```
destroyDecodePipeline():
  1. disconnect(m_decodeWorker, &DecodeWorker::frameDecoded, ...)  // 先切断信号
  2. requestStop()
  3. decodeThread->quit() → workLoop() 退出 → emit stopped()
  4. decodeThread->wait(3000)
  5. delete m_decodeWorker → m_decodeWorker = nullptr
  6. m_fallbackTimer->stop()（保底定时器此时可安全停止）

GLTextureViewport::cleanupGLResources():
  m_ringBuffer.cleanup():
    destroyPersistPbo()      // glDeleteBuffers + glUnmapBuffer
    for each slot:
      纹理不为 0 → glDeleteTextures
      state → Free
  // ... 着色器、VAO 等旧清理逻辑保持不变
```

#### 关键简化

- GL 操作全部在 GUI 线程 → 无跨线程 fence 依赖
- 无共享 GL 上下文 → 不需要在 DecodeThread 上做 cleanupGL
- fence 创建和删除都在同一个原生 GL 上下文中

### 5. SessionManager 变化

#### createDecodePipeline()

```cpp
void SessionManager::createDecodePipeline() {
    QThread* decodeThread = new QThread();
    decodeThread->start();

    m_decodeWorker = new DecodeWorker(nullptr);
    m_decodeWorker->moveToThread(decodeThread);
    m_decodeWorker->setOutputBuffer(m_glViewportForUpload->inputBuffer());

    // 连接 frameDecoded → doPreRender
    connect(m_decodeWorker, &DecodeWorker::frameDecoded,
            m_glViewportForUpload, &GLTextureViewport::doPreRender);

    // stopped / decodeError 信号同旧版
    decodeThread->setParent(m_decodeWorker);
    QMetaObject::invokeMethod(m_decodeWorker, "start", Qt::QueuedConnection);
}
```

#### 删除
- `m_pendingGLContext` — 不需要共享上下文
- `setGLContextForDecode()` — 废弃
- `frameBuffer()` — TripleBuffer<FrameSlot> 改为 TripleBuffer<DecodedFrame>（由 GLTextureViewport 持有）

### 6. FrameSlot → 废弃

旧 `FrameSlot` 包含 `remoteSize`, `arrivalTs`, `frameId`, `uploadFence`。新设计中这些信息分散在：
- `remoteSize` → `DecodedFrame::remoteSize`
- `frameId` → `DecodedFrame::frameId` + `TextureRingBuffer::Slot::frameId`
- `uploadFence` → `TextureRingBuffer::Slot::fence`（移除跨线程传递）
- `arrivalTs` → 可选后续重建延迟度量时加入

`FrameSlot.h` 可在确认无其他引用后删除。

## 线程模型

| 线程 | 组件 | 操作 |
|------|------|------|
| 网络线程 | SessionManager | `enqueueFrame()` 入队到 ThreadSafeQueue |
| DecodeThread | DecodeWorker | JPEG 解码 → TripleBuffer 输出 |
| GUI 线程 | GLTextureViewport | PreRender（PBO 上传 + fence）、pollFences、paintGL（纯绘制） |

## 风险评估

| 风险 | 缓解 |
|------|------|
| `doPreRender` 中 PBO memcpy 阻塞 GUI | PBO 持久映射的 memcpy 为微秒级；若需进一步优化可测量后决定 |
| 环满（3 槽全 InFlight）→ 丢帧 | 3 槽在正常 FPS 下足够；paintGL 消费速度远快于上传 |
| deltaReset 跳过帧 | 实时场景下这是预期行为；deltaReset 仅在旧帧 fence 长时间未触发时生效 |
| 保底定时器 + frameDecoded 信号双重触发 | doPreRender 内部判断 TripleBuffer 无新数据时仅做 pollFences，无副作用 |
