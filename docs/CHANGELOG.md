# 变更日志

## [2.0.0] — 2026-08-04

### 重大变更
- **协议重构**：HANDSHAKE/AUTH_CHALLENGE 移除 → VERSION_EXCHANGE + AUTHENTICATION 新握手流
- **TLS PKI 化**：TcpClient 从 VerifyNone + 指纹比对改为 VerifyPeer + 注入 CA 证书
- **ServerTrustStore**：指纹存储改为 PEM 证书存储
- **ErrorCode**：移除非故障码（VersionMismatch/Auth*/SessionNotAuthenticated）

### 新增
- 认证连接内重试模型：失败不断连、响应统一、全计数、阶梯锁定复活
- 版本不兼容对话框
- 凭据重输对话框（防重入守卫）

### 重构
- APP_VERSION 统一到 CMake project() — 通过 APP_VERSION_STR 宏传入，消除双源维护
- ConnectionManager 移除枚举改为事件信号 + VERSION_EXCHANGE/ENCODE_PREFS 适配
- ProtocolSession 移除 startSession 死守卫 + pipeline null 改断言
- ConnectionLifecycle 精简为纯传输层
- 服务端同步协议消息重命名 — VERSION_EXCHANGE/ENCODE_PREFS

### 修正
- m_authDialogPending 复位 + ACCESS_DENIED 重入守卫
- 对话框取消定时器 + TLS双错误抑制 + 证书清除
- TOFU 自动证书记录 + 错误态重连 + m_window 判空

## [1.0.0] — 基础版本

### 核心功能
- 屏幕捕获：DXGI / PipeWire / Qt GDI
- JPEG 编解码：CPU TurboJPEG + GPU NVJPEG / VA-API
- 远程键盘/鼠标输入转发（三平台）
- 剪贴板双向同步（文本 + 图片）
- 光标形状同步
- TLS 传输加密 + TOFU 证书信任 + PKI
- PBKDF2 密码认证
- 全屏模式 + 浮动工具栏
- 深色/浅色主题
- 连接历史 + 搜索
- 系统托盘
- 中英文国际化

### 架构
- 依赖注入（DI），无全局单例
- Worker 基类 + ThreadManager 统一线程管理
- 统一错误类型 RdError
- 日志六棵分类树

> 注：v1.0.0 的变更通过 git log 追溯，此处仅列出主要功能。
