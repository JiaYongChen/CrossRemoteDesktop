---
name: project_local_persistence_rule
description: 本地持久化数据的文件格式、存储位置、存取方式的统一规范
metadata: 
  node_type: memory
  type: project
  originSessionId: 14ebe3d7-ee16-4482-86eb-61c65d938e8c
---

所有本地持久化数据必须遵循以下规范：

**文件格式**：JSON（`QJsonObject` + `QJsonDocument` 序列化），不再使用 QSettings。

**存储位置**：可执行文件同目录（`QCoreApplication::applicationDirPath() + "/config.json"`），支持便携式部署。

**存取方式**：所有持久化操作统一通过 `Config` 类进行，`Config` 是唯一的文件读写入口。禁止在其他组件中直接创建 QSettings 或自行打开 JSON 文件。

**Config 类设计原则**：
- 底层存储为内存 `QJsonObject` + 文件序列化，不再依赖 QSettings
- 通过依赖注入（DI）传递，不使用单例模式
- 提供 `setValue()` / `value()` 等类型安全的公共 API
- 内置线程安全（QMutex）
- 内置自动迁移逻辑（首次启动时从旧 QSettings 迁移数据，完成后删除旧数据）

**ConnectionHistory 等结构体**：通过 `Config&` 存取，连接记录以 JSON 数组形式自然嵌套存储，不再使用并行 StringList 对齐方式。

**为什么**：QSettings 在不同平台自动选择不同后端（Windows 注册表、macOS plist、Linux INI），导致跨平台行为不一致，无法迁移数据。JSON 文件在所有平台行为完全一致，且对人类可读可编辑。

**如何应用**：新增任何本地持久化数据时，向 Config 类添加对应的 getter/setter 方法，调用方通过注入的 Config 引用存取，禁止绕过 Config 直接操作文件或 QSettings。
