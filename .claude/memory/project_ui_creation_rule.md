---
name: project_ui_creation_rule
description: 新建 UI 控件时优先使用 .ui 文件定义静态布局，动态逻辑保留在代码中
metadata: 
  node_type: memory
  type: project
  originSessionId: 77c0b433-136f-47a3-ab2a-cf399ce52ccb
---

新建或重构 UI 控件时，遵循以下分层策略：

- **静态布局 → .ui 文件**：控件的层级结构、固定属性（尺寸、间距、objectName、对齐方式等）使用 Qt Designer `.ui` 文件定义
- **动态逻辑 → C++ 代码**：动画（QPropertyAnimation 等）、信号连接、动态数据填充、依赖运行时的控件创建/销毁保留在代码中
- **边界判定**：如果一个控件在运行时不改变结构和存在性，就属于静态布局，应放入 .ui 文件。反之（如列表中的动态卡片、动画驱动的透明度效果）保留在代码中。

**Why**：统一项目中控件的创建方式，减少纯代码 `setupUi()` 的维护成本，同时保留代码对动态行为的完全控制。

**How to apply**：
1. 新建控件时先问：布局是静态的还是动态的？
2. 静态部分创建对应的 `.ui` 文件，放入 `src/ui/` 目录
3. 在 CMakeLists.txt 中添加 `.ui` 文件
4. 控件类使用 `Ui::Xxx *ui` 成员，构造函数中调用 `ui->setupUi(this)`
5. 参照 [[project_qss_layered_template]] 为控件编写对应的 QSS 规则
