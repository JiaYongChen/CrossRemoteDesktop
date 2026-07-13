---
name: 日志规范
description: Qt日志必须使用流式分类日志，所有分类集中在LoggingCategories.h/.cpp，禁止散落定义
type: feedback
---

## 日志输出格式

- **必须使用流式分类日志**：`qCInfo(lcXxx) << "message" << variable;`
- **禁止 printf 风格**：不要用 `qCInfo(lcXxx, "fmt %s", arg)`
- **禁止 QMessageLogger**：不要用 `QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO).info()`
- **禁止无分类日志**：不要用 `qWarning()`、`qDebug()`，必须使用 `qCWarning(lcXxx)`、`qCDebug(lcXxx)`

## 日志分类管理

- **所有分类集中定义**：`Q_DECLARE_LOGGING_CATEGORY` 放在 `src/common/core/logging/LoggingCategories.h`，`Q_LOGGING_CATEGORY` 放在对应的 `.cpp`
- **禁止在其他文件中散落定义** `Q_LOGGING_CATEGORY` 或 `Q_DECLARE_LOGGING_CATEGORY`
- 新增日志分类时，统一添加到 `LoggingCategories.h/.cpp`

## 头文件包含规则

- 使用日志分类的 `.cpp` 文件必须 `#include "LoggingCategories.h"`（用相对路径）
- `LoggingCategories.h` 已包含 `<QtCore/QLoggingCategory>`（其中也包含 `<QtCore/QDebug>`），因此：
  - `.cpp` 文件无需再单独包含 `<QtCore/QLoggingCategory>`、`<QtCore/QDebug>`、`<QtCore/QMessageLogger>`
  - `.h` 文件如果不直接使用日志宏，也无需包含 `<QtCore/QLoggingCategory>`
