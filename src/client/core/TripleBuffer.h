#pragma once

#include <atomic>
#include <memory>
#include <array>

template <typename T>
class TripleBuffer {
public:
    TripleBuffer() {
        m_slots[0] = std::make_unique<T>();
        m_slots[1] = std::make_unique<T>();
        m_slots[2] = std::make_unique<T>();
    }

    int acquireWrite(T*& out) {
        for (int i = 0; i < 3; ++i) {
            int candidate = (m_lastWrite + 1 + i) % 3;
            if (candidate != m_readSlot.load(std::memory_order_acquire)) {
                m_lastWrite = candidate;
                out = m_slots[candidate].get();
                return candidate;
            }
        }
        int candidate = (m_readSlot.load(std::memory_order_acquire) + 1) % 3;
        m_lastWrite = candidate;
        out = m_slots[candidate].get();
        return candidate;
    }

    void commitWrite(int slot) {
        m_readySlot.store(slot, std::memory_order_release);
    }

    int getReadSlot(T*& out) {
        const int ready = m_readySlot.load(std::memory_order_acquire);
        if (ready < 0 || ready == m_readSlot.load(std::memory_order_relaxed)) {
            return -1;
        }
        m_readSlot.store(ready, std::memory_order_release);
        out = m_slots[ready].get();
        return ready;
    }

    int peekReady() const {
        return m_readySlot.load(std::memory_order_acquire);
    }

    /// 重置缓冲区索引，防止在连接重置后读取过时帧数据。
    /// 使用 memory_order_seq_cst 确保跨线程的 happens-before 关系。
    void reset() {
        m_readSlot.store(-1, std::memory_order_seq_cst);
        m_readySlot.store(-1, std::memory_order_seq_cst);
        m_lastWrite = -1;
    }

private:
    std::array<std::unique_ptr<T>, 3> m_slots;
    std::atomic<int> m_readSlot{-1};
    std::atomic<int> m_readySlot{-1};
    int m_lastWrite = -1;
};
