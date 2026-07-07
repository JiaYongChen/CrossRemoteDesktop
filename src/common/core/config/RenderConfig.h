#pragma once

namespace RenderConfig {

enum class FrameDropPolicy {
    KeepOldest,   ///< Drop new frames when queue is full (legacy behavior)
    KeepLatest,   ///< Drop oldest frame when queue is full, keep newest
    LatestOnly,   ///< Queue capacity=1, always only hold the latest frame (lowest latency)
};

struct ClientFrame {
    FrameDropPolicy dropPolicy = FrameDropPolicy::LatestOnly;
    int queueCapacity = 5;   ///< Only used by KeepLatest; LatestOnly forces 1
};

struct GL {
    bool vsyncEnabled = true;
    bool usePbo = true;
    bool usePersistentPbo = true;
};

struct Client {
    ClientFrame frame;
    GL gl;
};

} // namespace RenderConfig
