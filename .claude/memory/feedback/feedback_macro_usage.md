---
name: 编译期特征宏使用规则
description: 编译期特征宏的使用规则——仅隔离真不可编译的场景，不冗余运行时检测
metadata: 
  node_type: memory
  type: feedback
  originSessionId: fc4fe2f3-f8b9-4fc4-a5d8-1409895dbf02
---

# 编译期特征宏（`HAS_XXX`）使用规则

## 核心原则

**编译期宏与运行时能力检测是同一道门的两个锁。当所有平台都能编译链接时，一道锁就够。**

## 允许使用 `#ifdef HAS_XXX` 的场景

- **某平台确实无法编译/链接**：例如 macOS 完全不支持某库、头文件不存在、链接库不存在
- **某平台需要不同的实现**（不仅是能力差异，而是 API 或行为根本不同）

## 禁止使用 `#ifdef HAS_XXX` 的场景

- **运行时已有能力检测机制**：如 `probeGPU()` → `isAvailable()`，无需再设编译期宏
- **"可选依赖"名存实亡**：当 `third_party/` 已为所有平台提供预编译文件，依赖不再是可选的
- **仅表达"此功能可能不可用"**：用运行时回退逻辑表达，不必用宏

## 判断流程

```
这个依赖在所有目标平台都存在编译文件吗？
├── 是 → 不用宏，运行时检测即可（如 nvJPEG 全平台预缓存时）
└── 否 → 用宏隔离不可编译的平台（如 CURRENT：macOS 无 CUDA 时）
```

## 具体示例

**反面（已移除的 `HAS_NVJPEG`）**：
```cpp
#ifdef HAS_NVJPEG  // 实际 ≡ Q_OS_WIN，增加无意义间接层
    auto nv = std::make_unique<NvJpegDecoder>();  // 冗余：运行时 isAvailable() 已足够
#endif
```

**正面（理想）**：
```cpp
auto nv = std::make_unique<NvJpegDecoder>();  // 无条件创建，probeGPU() 决定是否可用
if (nv->isAvailable()) { m_decoder = std::move(nv); }
```

## 参考资料

## 更新历史

- **2026-07-24**：`HAS_NVJPEG` 已从代码库移除，替换为 `Q_OS_WIN`；`NvJpegDecoder.h/cpp` 中的 stub 段也已一并删除。（`HAS_NVJPEG ≡ WIN32`，无独立语义价值。）
- **2026-06-25**（commit `50f9536`）：`HAS_OPENCL` 已从代码库移除，OpenCL 解码整套（源码、`third_party/opencl/`、测试）全部删除。
- `third_party/` 预缓存目录：所有平台的预编译文件均在此处统一管理
