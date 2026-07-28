---
name: 翻译系统维护规范
description: 翻译机制（.ts 持久跟踪 git 含译文；编译只 lrelease 生成 .qm 到 build/；lupdate 手动）
metadata: 
  node_type: memory
  type: project
  originSessionId: 65bf62d1-d8d8-414a-930b-afe073eb788d
---

# 翻译系统维护规范

## 核心机制（2026-07-28 定稿）

**.ts 是持久翻译源，.qm 是编译期产物：**

```
resources/translations/*.ts   ← 持久翻译源（含人工译文，跟踪 git）
   │  编译时自动 lrelease（release_translations ALL 目标）——不跑 lupdate
   ▼
build/translations/*.qm       ← 编译产物（build/ 已忽略，不进 git）
   │  运行时文件加载（路径由编译期注入 RD_TRANSLATIONS_DIR）
   ▼
QTranslator::load(locale.qm, RD_TRANSLATIONS_DIR)
```

- **编译时只跑 `lrelease`**（`.ts → .qm`），**不跑 lupdate**——避免重建 `.ts` 冲掉已填译文。
- **`.ts` 跟踪 git**（在 `resources/translations/`）：译文须跨编译/跨机器持久，故 `.ts` 是源而非产物。这是对早期"全部动态生成不进 git"的修正——译文要持久就必须进 git。
- **lupdate 手动**：源码 `tr()` 变更后跑 `update_translations` 刷新 `.ts`（保留已有译文，`-no-obsolete` 删废弃条目）。
- **qtbase 翻译**运行时从 Qt 安装目录加载（`QLibraryInfo::path(TranslationsPath)`），无需拷贝。
- `cmake/BootstrapTranslations.cmake` 已删除。

## 语言行为

- **中文源文本**：UI 一律 `tr("中文")`。`zh_CN.qm` 可空——中文模式回退到中文源即正确显示。
- **en_US.ts 已填充全部英文译文**（142 条），切换英文显示英文界面。
- **新增/改动 tr() 后**：跑 `update_translations` 刷新 .ts，并在 en_US.ts 补新条目的英文译文，再编译。

## 易错点

1. **源字符串语言须与默认语言（中文）一致**：英文源在空 zh_CN 翻译下会暴露英文（曾出现"中文模式显示英文按钮"）。语言选择器里的 "English"、"CPU/MB" 等技术词除外。
2. **编译不跑 lupdate**：改 tr() 后必须手动 `update_translations`，否则 .ts 不同步（但已填译文不会被冲）。
3. **`.ts` 是 XML**：填译文时 `&`/`<`/`>` 须转义为 `&amp;`/`&lt;`/`&gt;`；用脚本批量填充时 PowerShell 须以 pwsh 7（UTF-8）运行，Windows PowerShell 5.1 会按 GBK 误读中文。
4. **`<location>` 行号无需手动维护**：仅供 Qt Linguist 导航，lupdate 自动刷新。

## 翻译统计速查

```bash
grep -c 'type="unfinished"' resources/translations/en_US.ts   # 应为 0
```

## UIC 组件翻译

Qt `.ui` 文件静态文本经 UIC 生成的 `retranslateUi()` 翻译。**使用 .ui 且有 changeEvent 的类必须调用 `ui->retranslateUi(this)`**。

**How to apply:**
- 改 tr()/.ui 后：`update_translations` 刷新 .ts → 补 en_US 译文 → 编译（自动 lrelease）
- UI 文案用中文源 `tr("中文")`，避免英文源
- `.qm` 不进 git（build 产物）；`.ts` 进 git（持久译文）
