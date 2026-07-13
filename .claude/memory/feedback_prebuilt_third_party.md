---
name: feedback_prebuilt_third_party
description: 第三方库使用预编译包，不编译源码
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 3c41205c-164c-4d21-8e84-232a9c75a751
---

第三方库优先使用官方预编译二进制包（pre-built binaries），而不是通过 FetchContent/ExternalProject 从源码编译。

**Why:** 预编译包已经过优化（SIMD 等），避免了编译时间、汇编器依赖（如 NASM）和源码编译配置问题。集成速度更快，构建更可靠。

**How to apply:** 
- 第三方库的 cmake 模块应首先检查 `third_party/<lib>/` 下的预编译文件
- 如果缺失，应从官方源下载预编译包并提取到 `third_party/`
- 创建 IMPORTED 目标，像 OpenSSL 的处理方式一样
- 不自行编译源码
