---
name: QSS 三段式分层规范
description: QSS 文件三段式分层编写规范 — 修改或新增任何 QSS 规则时必须遵循此模板
metadata: 
  node_type: memory
  type: project
  originSessionId: 4435c973-a66f-4a5c-a88f-d06c39bc3909
---

# QSS 三段式分层编写规范

修改 `resources/styles/dark.qss` 或 `resources/styles/light.qss` 时必须遵守此规范。设计文档见 `docs/superpowers/specs/2026-07-03-qss-layered-rewrite-design.md`。

## 三层架构

每个 QSS 文件内部按三层组织：

### 第一层：全局基础
仅控制系统级控件：QToolTip, QMenu, QMenu::item, QMenu::separator, QStatusBar, QScrollBar（全局隐藏）, QDialog（仅 background-color）, selection-background-color。

**禁止出现**：`QWidget {}`、`QLabel {}`、`font-size`、`font-weight`。

### 第二层：类型默认样式
定义每种控件类型的"出厂外观"，无父选择器限定。
覆盖：QLineEdit, QComboBox, QSpinBox, QPushButton, QCheckBox, QGroupBox, QTabWidget/QTabBar, QListWidget, QSlider, QTextEdit, QScrollBar（对话框用）。

规则：
- 每个规则集只覆盖自身盒子属性（border, border-radius, padding, background）
- 不设 color / font-size（留给第三层）
- 每个控件必须覆盖 :hover, :focus, :pressed, :disabled 全状态

### 第三层：区域样式覆盖
以 `#objectName` 开头限定作用域，按 `.ui` 源文件分组，组内父→子→孙排列。

**命名格式**：
```css
/* === [源文件.ui] #父控件 === */
#父控件 { }
#父控件 > 直接子 { }
#父控件 后代 { }
```

**规则**：
- 禁止出现无父选择器的裸类型选择器
- 不使用 `!important`
- 同一 `.ui` 文件的区域连续排列，不与其他 `.ui` 文件交叉

## 设计令牌

每个 QSS 文件顶部以注释块列出全部设计令牌。每个属性值行末标注令牌：

```css
/* --color-bg-page: #1E1E1E */
/* --radius-md: 6px */

QMainWindow {
    background-color: #1E1E1E;               /* token: --color-bg-page */
}
```

## .ui 文件与 objectName 映射

| .ui 文件 | 主要 `#objectName` |
|----------|-------------------|
| `mainwindow.ui` | leftNavBar, sideSeparator, headerArea, titleLabel, logoLabel, connectionPanel, hamburgerItem, exitItem, statusbar |
| `ConnectionDialog.ui` | ConnectionDialog, tabContainer, tabWidget, serverGroup, authGroup, displayGroup, qualityGroup, featuresGroup, networkGroup, buttonBox, connectionTab, optionsTab, advancedTab |
| `settingsdialog.ui` | SettingsDialog, sidebarWidget, categoryListWidget, mainContentWidget, generalPage, serverPage, advancedPage, languageGroup, startupGroup, closeBehaviorGroup, serverNetworkGroup, serverAuthGroup, loggingGroup, presetDebugBtn, resetRulesBtn, restoreDefaultsBtn, buttonWidget |

## 双文件对称要求

- `dark.qss` 和 `light.qss` 的段落顺序、选择器、注释完全一致
- 仅设计令牌值和具体颜色不同
- 修改一个文件时，必须同步更新另一个文件的对应段落

## 加载策略

- `qApp->setStyleSheet()` + `MainWindow::setStyleSheet()` 双重加载（保持现有逻辑）
- 不合并为单文件
- 不引入 SCSS 等预处理器

**Why:** CLUAIDE.md 中未记录 QSS 组织规范，当前文件缺乏层级约束。三段式分层确保样式作用域可控，`.ui` 分组注释保证可追溯性，令牌注释保证跨文件一致性。

**How to apply:** 新增或修改任何 QSS 规则时，先确定归属层级（全局/类型/区域），再找到对应段落插入。区域规则必须标注 `[源文件.ui] #父控件` 注释。
