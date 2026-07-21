---
name: 构建验证用 grep 扫描错误
description: 构建/测试验证必须用 grep 过滤 error 而非 tail 截断
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 8f92dde2-5c1f-40c5-8650-acbe7373555e
---

构建验证不能仅依赖 `cmake --build ... | tail -5` 截取末尾输出——MSBuild/VCXProj
按项目顺序编译，主程序错误可能出现在输出中间，tail 只看到最后的测试目标编译成功。

正确做法：
```bash
cmake --build build --config Debug 2>&1 | grep -iE "error C|error:" | head -5
```
输出为 0 行才算通过。测试同样要检查完整结果而非只截尾。

**Why:** `cmake -B build` 重配置会刷新 generate.stamp 触发全量重编译，
之前的增量构建可能跳过了有问题文件。任何时候构建验证都应该检测所有错误信息。
**How to apply:** 每次构建后用 grep 扫描完整输出中的 error，0 行才确认通过。
