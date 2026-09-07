---
name: 构建验证保留完整输出和退出码
description: 构建/测试验证必须检查完整输出及原始退出码，禁止只看 tail
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 8f92dde2-5c1f-40c5-8650-acbe7373555e
---

构建验证不能仅依赖 `cmake --build ... | tail -5` 截取末尾输出——MSBuild/VCXProj
按项目顺序编译，主程序错误可能出现在输出中间，tail 只看到最后的测试目标编译成功。

正确做法是保留完整日志，并在扫描错误的同时单独保存原始构建退出码：

```bash
set -o pipefail
build_log="$(mktemp)"
trap 'rm -f "$build_log"' EXIT

cmake --build build --config Debug 2>&1 | tee "$build_log"
build_status="${PIPESTATUS[0]}"
grep -iE "error C|error:|CMake Error" "$build_log" | head -5 || true
test "$build_status" -eq 0
```

构建退出码为 0 且错误扫描无有效错误才算通过。测试同样要检查完整结果而非只截尾。

使用项目构建脚本时同样必须检查脚本的最终退出码和完整输出，例如当前已验证的 Linux 使用 `./build.sh --run-tests`，Windows 使用 `./build.ps1 -RunTests`。macOS CMake/Objective-C++ 接入已完成，但仍需补齐缓存后才能用该脚本做项目验证。不要因为末尾出现“构建成功”或某个测试目标成功就忽略前面的错误。

CTest 按名称或标签筛选时，仅检查退出码仍不够：默认情况下零匹配也可能返回 0。直接调用时加 `--no-tests=error`；当前 `build.sh` / `build.ps1` 以及 CMake 聚合测试目标均已传递该选项。

**Why:** `cmake -B build` 重配置会刷新 generate.stamp 触发全量重编译，
之前的增量构建可能跳过了有问题文件。任何时候构建验证都应该检测所有错误信息。
**How to apply:** 每次构建后同时检查原始退出码与完整日志；需要过滤时先保存日志，再用 grep/rg 扫描，不能让管道覆盖构建命令的退出码。测试筛选还必须用 `--no-tests=error` 或显式核对非零执行数量。
