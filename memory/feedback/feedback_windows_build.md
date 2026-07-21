---
name: Windows 构建注意事项
description: Windows 下必须使用 VS 生成器（非 Ninja），需 /FS 避免 PDB 锁冲突
metadata:
  node_type: memory
  type: feedback
---

Windows 构建必须使用 Visual Studio 生成器（MSBuild），不要使用 Ninja 生成器。

**Why:**
1. Ninja 直接调用 cl.exe 时依赖 shell 环境中的 INCLUDE 变量来找 UCRT 标准头文件（stddef.h、limits.h 等）。如果不是从 Developer Command Prompt 启动，INCLUDE 为空，zstd 等 C 语言子项目编译会报 `fatal error C1083: 无法打开包含文件 stddef.h`。
2. Visual Studio 生成器（MSBuild）能自动设置正确的 include/lib 路径，不依赖环境变量。
3. 并行编译时多个 cl.exe 竞争同一 PDB 文件会导致 `C1041` 错误，已通过在 CMakeLists.txt 中添加 `/FS` 编译选项解决。

**How to apply:**
- 配置命令：`cmake -B build -DCMAKE_BUILD_TYPE=Debug`（不指定 `-G Ninja`，让 CMake 自动选择 VS 生成器）
- 如果遇到 zstd 编译报找不到标准头文件，先删除 `build/` 目录重新配置
- MSVC 编译选项中始终包含 `/FS` 以避免 PDB 锁冲突
- 当前 VS 环境：Visual Studio 18 2026，MSVC 19.50，Windows SDK 10.0.26100.0
- Qt 路径：`C:/Qt/6.9.3/msvc2022_64`
