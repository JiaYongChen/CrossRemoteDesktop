---
name: 翻译系统维护规范
description: 翻译文件维护的完整流程和常见问题，修改 tr() 字符串后必须遵循
metadata: 
  node_type: memory
  type: project
  originSessionId: 65bf62d1-d8d8-414a-930b-afe073eb788d
---

# 翻译系统维护规范

## 核心规则

**修改源码中的 `tr()` 调用或 `.ui` 文件后，必须执行两步：**

```bash
# 第 1 步：扫描源码，更新 TS 文件（记录新字符串 + 更新行号）
cmake --build build --target update_translations

# 第 2 步：编译 TS → QM（生成运行时加载的二进制翻译文件）⚠️ 最容易遗漏！
cmake --build build --target release_translations

# 或者一键完成两步：
cmake --build build --target sync_translations
```

## 为什么"以前的版本能正常切换"

### 断裂链（2026-07-13 本次会话）

```
步骤 1: 修改源码（新增 About 对话框、品牌改名）
步骤 2: ✅ 运行 lupdate → TS 文件更新
步骤 3: ❌ 未运行 lrelease → QM 文件过期（6月15日/6月30日的旧文件）
步骤 4: 运行时加载旧 QM → 新字符串无翻译 → 回退显示中文源文本
```

### 为何旧版本正常

旧版本中：
- 源码中的 `tr()` 字符串（如"Qt远程桌面"）在**旧 QM** 中有对应的英文翻译
- 旧 QM 文件的翻译覆盖率足够日常使用
- 本次会话引入了 **30+ 条新 `tr()` 字符串**（品牌改名 + 许可证声明）
- 旧 QM 中这些新字符串自然不存在 → 翻译失效

## 翻译文件三层架构

```
 resources/translations/
 ├── *.ts      ← 源码（lupdate 生成/更新，提交 git）
 ├── *.qm      ← 二进制（lrelease 编译，必须同步提交 git！）
 └── CMake 目标
     ├── update_translations   ← lupdate（扫描源码 → 更新 TS）
     ├── release_translations  ← lrelease（编译 TS → QM）
     └── sync_translations     ← 一键完成上述两步
```

## 新增 tr() 字符串后的检查清单

- [ ] 运行 `sync_translations`（或分别运行 lupdate + lrelease）
- [ ] `git diff` 确认 .ts 和 .qm 都被更新
- [ ] 运行 `lrelease <ts_file> -qm /tmp/test.qm` 检查翻译统计
- [ ] 对于 en_US.ts，确保 `type="unfinished"` 条目数为 0
- [ ] 如果新增了需要翻译的字符串，**立即填写英文翻译再提交**
- [ ] `.ts` 和 `.qm` 文件**一起提交** git

## 易错点

1. **`lupdate` 只更新 TS，不编译 QM** — 这是最常见的遗漏。TS 是源码，QM 是运行时使用的二进制。只改 TS 不改 QM 等于没改。
2. **TS 文件提交了但 QM 未提交** — 协作者拉取后看到的是旧 QM，本地构建不会自动重新编译。
3. **新字符串未填写翻译** — lupdate 提取后默认 `type="unfinished"`，运行时回退显示源语言（中文）。对英语用户来说就是中文残留。
4. **误以为需手动维护 TS `<location>` 行号** — TS 文件中 `<location>` 行号仅给 Qt Linguist 导航用，不影响运行时翻译，无需手动更新。`lupdate` 会自动刷新行号。
5. **担心 lupdate 覆盖已有翻译** — `lupdate` 保留已有译文不变（仅新增缺失条目、标记废弃条目为 `type="vanished"`），安全可重复执行。

## 翻译统计速查

```bash
# 检查 TS 文件中未翻译条目数
grep -c 'type="unfinished"' resources/translations/en_US.ts

# 编译并查看统计（不覆盖正式文件）
/path/to/lrelease resources/translations/en_US.ts -qm /tmp/test.qm
# 输出示例: "Generated 133 translation(s) (133 finished and 0 unfinished)"
#                                       ^^^              ^^^
#                                    总数完成          未完成数应为0
```

## UIC 组件翻译

Qt `.ui` 文件中的静态文本通过 UIC 生成的 `retranslateUi()` 翻译。**每个使用 .ui 文件且有 changeEvent 的类必须调用 `ui->retranslateUi(this)`**：

| 组件 | .ui 文件 | 翻译方式 | 状态 |
|------|:--:|------|:--:|
| MainWindow | ✅ | `retranslateUi()` → `ui->retranslateUi(this)` | ✅ 已修复 |
| SettingsDialog | ✅ | `changeEvent()` → `ui->retranslateUi(this)` | ✅ 正常 |
| ConnectionDialog | ✅ | `changeEvent()` → `ui->retranslateUi(this)` | ✅ 已修复 |
| ConnectionPanel | ✅ | 手动 `tr()` 覆盖 | ✅ 正常 |
| NavPanel | ❌ | 手动 `tr()` | ✅ 正常 |

**How to apply:**
- 每次修改 `tr()` 或 `.ui` 后运行 `sync_translations`
- 检查 git diff 确认 .ts 和 .qm 都已更新
- 新字符串立即填写翻译再提交
- 新增 .ui 文件后检查对应类的 changeEvent 是否调用了 UIC retranslate
