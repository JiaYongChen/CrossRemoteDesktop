---
name: project_constants_organization_rules
description: 常量组织与命名规范，新增或修改常量时必须遵守
metadata: 
  node_type: memory
  type: project
  originSessionId: 441b5fcf-bb6b-47f4-80d9-1045208772e8
---

# 常量组织规范

## 声明形式

所有跨模块公共常量使用 `namespace` + `inline constexpr`（C++17 inline 变量支持头文件定义）：

```cpp
namespace SomeDomainConstants {
inline constexpr int SomeConstant = 42;
} // namespace SomeDomainConstants
```

**不使用** `struct` 嵌套、`class` + 私有构造、`static const` 成员、`#define` 宏。

## 命名规范

- **常量名**：`PascalCase`（首字母大写驼峰），如 `DefaultFrameRate`、`MaxQueueSize`
- **namespace 名**：`PascalCase` + `Constants` 后缀，如 `ProtocolConstants`、`CaptureConstants`
- **枚举值**：`PascalCase`，如 `FrameDropPolicy::LatestOnly`
- **函数名**：`PascalCase`，如 `GetVersionString()`、`IsValidPort()`

## 常量文件（6 个，全部位于 `src/common/config/`）

| 文件 | 归属领域 |
|------|----------|
| `ProtocolConstants.h` | 网络协议/帧结构/消息格式/字段长度上限/应用版本 |
| `NetworkConstants.h` | TCP 连接/端口/超时/心跳/重连/Keep-Alive |
| `CaptureConstants.h` | 屏幕捕获/JPEG 编码/缩放因子/输入设备参数/DXGI |
| `ProcessingConstants.h` | 线程/队列/缓冲区/性能监控/数据处理/并发控制 |
| `SecurityConstants.h` | 加密参数/认证/会话超时/速率限制/密码学常量 |
| `GuiConstants.h` | 窗口尺寸/控件/OpenGL 渲染/PBO/帧丢弃策略 |

## 归属决策树

新增常量按以下决策选择文件：

```
常量属于 →
├─ 网络协议/帧结构/消息格式 → ProtocolConstants.h
├─ TCP/端口/超时/心跳/重连     → NetworkConstants.h
├─ 屏幕捕获/编码/缩放/输入     → CaptureConstants.h
├─ 线程/队列/缓冲区/性能/并发  → ProcessingConstants.h
├─ 加密/认证/会话/速率限制     → SecurityConstants.h
└─ 窗口/控件/OpenGL/渲染/显示  → GuiConstants.h
```

## 作用域规则

- **仅单个类内部使用** → 保持为该类的 `static constexpr` 私有成员，不必放入公共常量文件
- **2 个及以上文件引用** → 必须放入对应的公共常量文件，消除重复
- **运行时可通过配置修改** → 常量仅定义默认值；实际运行时值由 `Config` / `SettingsManager` 管理

## 禁止事项

- 禁止使用 `#define` 定义常量（仅用于平台兼容的条件编译 `#ifdef`）
- 禁止在 `.cpp` 文件中定义公共常量（其他翻译单元无法引用）
- 禁止在常量 namespace 中定义非 constexpr 的运行时变量
- 禁止在同一项目中重复定义语义相同的常量
- **禁止将 UI 文本/日志消息等面向用户的字符串常量放入 Constants 文件** — 直接内联在调用点（UI 文本用 `tr()` 保持多语言支持，日志文本用字面量）。例外：加密套件名称、默认用户名等非用户可见的配置字符串允许放在 SecurityConstants 中。
- 禁止使用 `static const` 成员（用 `inline constexpr` 替代）

## 类型规范

- 整型常量使用 `int` 或明确位宽的 `quint32`/`quint64`（协议相关必须精确宽度）
- 浮点常量使用 `double`，除非有明确的存储约束
- 编译期常量使用 `constexpr`，运行时常量使用 `const`（如 `inline const QString`）
- 所有常量文件为纯头文件，无需对应的 `.cpp`

**Why:** 项目常量当前存在重复定义（STATS_UPDATE_INTERVAL 在 4 处独立定义）、三种风格混用（struct/namespace/class）、命名不统一（UPPER_CASE/kPascalCase/PascalCase）等问题。统一后降低认知负担，消除重复，让 include 关系自然反映模块依赖。

**How to apply:** 新增常量时参照归属决策树选择文件，使用 `namespace` + `inline constexpr` + `PascalCase`。修改现有常量时发现重复定义即合并到公共文件。Code review 时检查是否有 `#define` 常量或 `.cpp` 中定义常量的情况。
