---
name: 翻译系统维护规范
description: 翻译机制（.ts/.qm 全部动态生成到 build/、均不进 git，运行时加载）与维护流程
metadata: 
  node_type: memory
  type: project
  originSessionId: 65bf62d1-d8d8-414a-930b-afe073eb788d
---

# 翻译系统维护规范

## 核心机制（2026-07-28 重构）

**翻译产物全部动态生成到 `build/translations/`、均不纳入 git，运行时文件加载：**

```
源码 tr() / .ui
   │  lupdate 扫描（update_translations 手动目标；
   │   或 .ts 缺失时由 cmake/BootstrapTranslations.cmake 在编译前自动引导）
   ▼
build/translations/*.ts       ← lupdate 生成（build/ 已忽略，不进 git）
   │  每次编译自动 lrelease（release_translations ALL 目标 + add_dependencies）
   ▼
build/translations/*.qm       ← lrelease 编译产物
   │  运行时文件加载（路径由 CMake 编译期注入 RD_TRANSLATIONS_DIR）
   ▼
QTranslator::load(locale.qm, RD_TRANSLATIONS_DIR)
```

- **`.ts` 与 `.qm` 都在 `build/translations/`，都不进 git。** `resources/translations/` 已废弃删除。
- **app 翻译**（zh_CN/en_US）：运行时从 `build/translations/` 加载（非资源嵌入，.qrc 无 .qm）。
- **qtbase 翻译**：运行时从 Qt 安装目录加载（`QLibraryInfo::path(TranslationsPath)`），无需拷贝/分发。
- **全新 clone 自动引导**：`.ts` 缺失时 `BootstrapTranslations.cmake`（经 `cmake -P` 调用）先跑 lupdate 生成骨架，避免 lrelease 找不到输入失败。源码列表经 `build/translation_sources.txt`（每行一路径）传递——`-D` 传分号列表会被截断。

## ⚠️ 重要权衡（用户已确认接受）

`.ts` 不进 git 意味着**翻译内容不随仓库分发**：全新 clone / 清理 build 后，lupdate 只能从源码重建 `.ts` 骨架（新条目 `type="unfinished"`），已有英译不会自动出现，运行时回退源语言（中文）。译文仅存在于生成过它的本地 build 目录。需跨设备共享译文须另行备份 `.ts`。

## 维护流程

修改源码 `tr()` 调用或 `.ui` 文件后：

```bash
# 第 1 步：扫描源码刷新 build/translations/*.ts（行号 + 新条目）
cmake --build build --target update_translations

# 第 2 步：正常编译，lrelease 自动重新生成 .qm
cmake --build build --config Debug
```

## 检查清单

- [ ] 运行 `update_translations` 刷新 `.ts`
- [ ] 新字符串立即在 `.ts` 中填写译文（默认 `type="unfinished"`）
- [ ] en_US.ts 的 `type="unfinished"` 条目数应为 0
- [ ] 新增 .ui 文件后检查对应类 `changeEvent` 是否调用 `ui->retranslateUi(this)`
- [ ] `.ts`/`.qm` 均**不**提交 git（build/ 已忽略）；需保留译文请本地备份

## 易错点

1. **新字符串未填译文** — lupdate 提取后默认 `type="unfinished"`，运行时回退中文。
2. **担心 lupdate 覆盖译文** — `lupdate` 保留已有译文（仅新增缺失条目、废弃条目标记 `type="vanished"`），安全可重复执行。
3. **`<location>` 行号无需手动维护** — 仅供 Qt Linguist 导航，lupdate 自动刷新。
4. **清理 build/ 会丢译文** — `.ts` 在 build/ 内，clean 或删除 build 后译文消失，重新引导生成的是 unfinished 骨架。

## 翻译统计速查

```bash
grep -c 'type="unfinished"' build/translations/en_US.ts
```

## UIC 组件翻译

Qt `.ui` 文件静态文本经 UIC 生成的 `retranslateUi()` 翻译。**使用 .ui 且有 changeEvent 的类必须调用 `ui->retranslateUi(this)`**。

**How to apply:**
- 修改 `tr()`/`.ui` 后运行 `update_translations`，再正常编译（.qm 自动生成）
- `.ts` 与 `.qm` 都是 build/ 下的动态产物，绝不提交 git
- 新字符串立即填译文；译文需跨设备/防 clean 时另行备份
