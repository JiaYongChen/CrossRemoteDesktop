# Memory Index

## User
- [user_profile.md](user_profile.md) — 熟练的 C++/Qt 开发者，中文沟通，直接高效

## Project
- [project_overview.md](project_overview.md) — UltraDesktop（极域桌面）项目核心目标和架构方向
- [project_qss_layered_template.md](project_qss_layered_template.md) — QSS 三段式分层编写规范，修改或新增 QSS 规则时必须遵守
- [project_ui_creation_rule.md](project_ui_creation_rule.md) — 新建 UI 控件优先用 .ui 文件定义静态布局，动态逻辑保留在代码中
- [project_local_persistence_rule.md](project_local_persistence_rule.md) — 本地持久化数据统一使用 JSON 文件格式，通过 Config DI 存取，位于可执行文件同目录
- [project_constants_organization_rules.md](project_constants_organization_rules.md) — 常量组织与命名规范，新增或修改常量时必须遵守
- [project_directory_organization_rules.md](project_directory_organization_rules.md) — 源码目录组织规范，新增或移动文件时必须遵守
- [project_i18n_workflow.md](project_i18n_workflow.md) — 翻译机制：.ts 持久跟踪 git（含译文），编译期 lrelease 生成 .qm（不跑 lupdate）
- [project_include_forward_declare_rules.md](project_include_forward_declare_rules.md) — .h 文件中 #include 与前向声明的判断规则
- [project_auth_challenge_removal_deferred.md](project_auth_challenge_removal_deferred.md) — 移除 AUTH_CHALLENGE 提议于 2026-07-29 暂缓；系异步 PBKDF2 盐值投递载体，用户或重提

## Feedback
- [feedback_logging_rules.md](feedback/feedback_logging_rules.md) — Qt 日志必须使用流式分类日志，分类集中在 LoggingCategories.h/.cpp
- [feedback_use_chinese.md](feedback/feedback_use_chinese.md) — 会话全程中文，包括代码注释
- [feedback_no_feature_branches.md](feedback/feedback_no_feature_branches.md) — 直接在 master 上提交，不创建功能分支
- [feedback_prefer_qt_builtin.md](feedback/feedback_prefer_qt_builtin.md) — Qt 已包含则用内置；若内置不完整则完全由第三方库替代
- [feedback_third_party_libs.md](feedback/feedback_third_party_libs.md) — 预编译包缓存于 third_party/ 并提交 git，CMake 直接使用不调 vcpkg
- [feedback_no_docs_in_git.md](feedback/feedback_no_docs_in_git.md) — 不要将 docs/ 目录下的文件添加到 git 追踪
- [feedback_macro_usage.md](feedback/feedback_macro_usage.md) — 编译期宏仅隔离真不可编译的平台，不冗余覆盖运行时能力检测
- [feedback_commit_style.md](feedback/feedback_commit_style.md) — Git 提交中文，格式：`类型: 描述`
- [feedback_windows_build.md](feedback/feedback_windows_build.md) — Windows 必须用 VS 生成器（非 Ninja），需 /FS 避免 PDB 锁冲突
- [feedback_bash_heredoc_commit.md](feedback/feedback_bash_heredoc_commit.md) — Git Bash 中用 POSIX heredoc + git commit -F
- [feedback_build_verification.md](feedback/feedback_build_verification.md) — 构建验证：grep error 而非 tail 截断
- [feedback_auto_detect_paths.md](feedback/feedback_auto_detect_paths.md) — 项目中所有路径必须使用自动检测，禁止硬编码绝对路径
- [feedback_windows_git_case.md](feedback/feedback_windows_git_case.md) — Windows 上 git 跟踪路径大小写与磁盘不符致永久幻影 M，用 git mv 纠正
- [feedback_flaky_latency_test.md](feedback/feedback_flaky_latency_test.md) — FrameTransmissionLatencyTest 套件并行跑偶发失败(负载致延迟超阈值),单独重跑即过,非回归
- [feedback_avoid_dynamic_commands.md](feedback/feedback_avoid_dynamic_commands.md) — 避免 $变量/& 开头的动态表达式命令(Claude Code 硬闸弹窗,hook allow 无法豁免);用静态前缀/pwsh -File/auto mode
