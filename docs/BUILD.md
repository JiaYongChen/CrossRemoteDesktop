# 构建指南

## 依赖

| 依赖 | 版本要求 | 获取方式 |
|------|---------|---------|
| Qt | 6.9+（Linux 已验证 6.9.3） | Qt 官方 SDK / aqtinstall |
| OpenSSL | 3.6.2 | `third_party/` 缓存 |
| libjpeg-turbo | 3.1.4.1 | `third_party/` 缓存 |
| nvJPEG / CUDA Runtime | CUDA 12.x（仅 Windows） | `third_party/` 缓存；完整 Toolkit 仅用于维护缓存 |
| CMake | 3.21+ | [cmake.org](https://cmake.org/download/) |
| C++ 编译器 | C++20 支持 | MSVC 2022+ / GCC 13+ / Clang 16+ |

## 获取源码

```bash
git clone <repo-url>
cd CrossRemoteDesktop
```

## 第三方库

项目使用 [vcpkg](https://github.com/microsoft/vcpkg) 获取预编译依赖，产物缓存于 `third_party/` 并提交 git。CMake 构建时直接使用缓存，不调用 vcpkg；当前 Windows x64 和 Linux x64 缓存完整，macOS 各架构的已知缺口见下文。

OpenSSL 的公共头文件位于 `third_party/openssl/include/openssl/`；各平台仅在 `third_party/openssl/include/<OS>-<Arch>/openssl/configuration.h` 保存平台配置头，构建时平台目录优先于公共目录。

项目目标应链接 `OpenSSL::SSL` / `OpenSSL::Crypto`，或在确需路径变量时使用包含平台目录和公共目录的 `OPENSSL_INCLUDE_DIRS`。为避免误用缺少平台 `configuration.h` 的单一路径，项目不导出 `OPENSSL_INCLUDE_DIR`。

Linux x64 缓存使用 `x64-linux` triplet 构建，包含 OpenSSL 3.6.2（Debug/Release 静态库及 Linux 专用 `configuration.h`）和 libjpeg-turbo 3.1.4.1（静态库）。可复现基线为 vcpkg commit `7ff71c68261ecf3b60cdb8772f2e6759ee0a71b1`。

开发者首次设置：Windows 运行 `.claude\scripts\setup-symlinks.bat`；macOS/Linux 运行 `chmod +x .claude/scripts/setup-symlinks.sh && ./.claude/scripts/setup-symlinks.sh`。

## 构建

### 跨平台参数约定

两个脚本使用相同的 kebab-case 参数词汇。Linux/macOS 遵循 POSIX 风格使用双横线长参数，PowerShell 遵循自身语法使用单横线；例如 `--run-tests` 对应 `-run-tests`。参数和值统一使用空格分隔。

| 用途 | Linux / macOS (`build.sh`) | Windows (`build.ps1`) |
|------|----------------------------|-----------------------|
| 构建配置 | `-c, --configuration` | `-c, -configuration` |
| 构建目录 | `-B, --build-directory` | `-B, -build-directory` |
| 构建目标 | `-t, --target` | `-t, -target` |
| 并行任务数 | `-j, --jobs` | `-j, -jobs` |
| 构建后测试 | `--run-tests` | `-run-tests` |
| 测试标签 | `--test-label` | `-test-label` |
| 测试名称正则 | `--test-regex` | `-test-regex` |
| 仅配置 | `--configure-only` | `-configure-only` |
| 跳过配置 | `--skip-configure` | `-skip-configure` |
| CMake 生成器 | `-G, --generator` | `-G, -generator` |
| Qt SDK 根目录 | `--qt-root` | `-qt-root` |
| 额外 CMake 前缀 | `--prefix-path` | `-prefix-path` |
| 额外 CMake 参数 | `--cmake-argument` | `-cmake-argument` |
| 帮助 | `-h, --help` | `-h, -help` |

PowerShell 参数不区分大小写，原有的 `-RunTests`、`-BuildDirectory`、`-QtRoot` 等写法继续兼容；新文档统一使用上表形式。Bash 的目标、前缀和 CMake 参数可通过重复选项传入多个值；PowerShell 在交互式会话中使用逗号数组，例如 `-target target_a,target_b`。从其他 Shell 通过 `powershell.exe -File` 或 `pwsh -File` 调用时，公共用法建议一次只传一个数组值。

### Windows

Windows 使用 VS 2022 生成器，因此需要 CMake 3.21+。推荐使用仓库根目录的 PowerShell 构建脚本（兼容 Windows PowerShell 5.1 和 PowerShell 7）：

```powershell
./build.ps1
./build.ps1 -configuration Release -jobs 8
./build.ps1 -run-tests
./build.ps1 -qt-root C:\Qt\6.9.3\msvc2022_64
./build.ps1 -help
```

在其他 Shell 中可改用 `powershell.exe -File ./build.ps1` 或 `pwsh -File ./build.ps1`。默认生成器不是 Visual Studio 时，传入 `-generator 'Visual Studio 17 2022'`。也可直接调用 CMake：

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

必须使用 VS 生成器（非 Ninja），需要 `/FS` 避免 PDB 锁冲突。

### macOS

macOS 的 Objective-C++ 平台实现和 CMake 接入已完成：Apple 构建会启用 `OBJCXX`、收集 `.mm` 源文件，并在目标创建后链接 ScreenCaptureKit、ImageIO/CoreGraphics 等框架。应用以 `.app` bundle 生成，`Info.plist` 含屏幕录制用途说明；首次捕获会触发系统授权。ScreenCaptureKit 初始化失败时回退 Qt QScreen；ImageIO/CoreGraphics 解码失败时由工作线程回退 TurboJPEG。

当前仍被第三方缓存阻塞：x64 缺少 `third_party/libjpeg-turbo/lib/macOS-x64/libturbojpeg.a` 和 OpenSSL Debug 静态库；ARM64 尚未提供 OpenSSL/libjpeg-turbo 平台缓存。因此上述接入尚未在 macOS 构建和真机运行中验证。

推荐安装 Qt 6.9.3 Desktop SDK 和 ImageFormats 模块（完整测试中的 WEBP 编解码需要该模块）：

```bash
uvx --from aqtinstall==3.3.0 aqt install-qt \
  --outputdir "$HOME/Qt" \
  mac desktop 6.9.3 clang_64 \
  --modules qtimageformats
```

`build.sh` 的参数解析、路径处理和作业数检测兼容 macOS 自带 Bash 3.2；补齐当前宿主架构缓存后，调用形式为：

```bash
./build.sh
./build.sh --run-tests
./build.sh --qt-root "$HOME/Qt/6.9.3/macos"
```

脚本和 CMake 当前按宿主机架构选择依赖缓存，仅计划支持原生单架构构建，不支持 Apple Silicon 上的 x86_64 交叉构建或 Universal 构建。补齐缓存后，需要直接排障时也可调用 CMake：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

### Linux

以 Ubuntu 为例，先安装编译器和平台开发头：

```bash
sudo apt install build-essential cmake pkg-config libgl1-mesa-dev libx11-dev libxtst-dev
cmake --version
```

请确认安装结果为 CMake 3.21 或更高版本；发行版软件源版本过旧时，需要改用新版 CMake 安装源或官方发行包。

当前已验证的 Linux 基线使用 `QScreen::grabWindow` 捕获和 CPU TurboJPEG 解码。PipeWire 与 VA-API 仅保留 fail-closed 占位实现，CMake 不链接依赖、也不定义 `HAS_PIPEWIRE` / `HAS_VAAPI`；即使系统已安装相应开发包，也不会误启用未完成后端。未来完成门户授权、流生命周期和有效 VA 解码上下文后，再显式接入。

推荐安装 Qt 6.9.3 Desktop SDK 和 ImageFormats 模块（测试中的 WEBP 编解码需要该模块）：

```bash
uvx --from aqtinstall==3.3.0 aqt install-qt \
  --outputdir "$HOME/Qt" \
  linux desktop 6.9.3 linux_gcc_64 \
  --modules qtimageformats
```

推荐使用仓库根目录的 `build.sh`；当前已验证平台为 Linux。脚本也兼容 Darwin，但 macOS 缓存缺口见上节。在其他目录可通过脚本实际路径调用：

```bash
./build.sh
./build.sh --configuration Release --jobs "$(nproc)"
./build.sh --run-tests
./build.sh --target test_threadmanager --run-tests --test-regex ThreadManagerTest
./build.sh --qt-root "$HOME/Qt/6.9.3/gcc_64"
```

使用 `./build.sh --help` 可查看目标、测试筛选、生成器及额外 CMake 参数等完整选项。也可直接调用 CMake：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.9.3/gcc_64"
cmake --build build --config Debug --parallel
```

若无系统包安装权限，可把 OpenGL/X11 开发包解压到用户目录，并通过可重复的 `--prefix-path` 传入额外前缀：

```bash
./build.sh \
  --qt-root "$HOME/Qt/6.9.3/gcc_64" \
  --prefix-path "$HOME/.local/share/crossrd-linux-sdk/usr"
```

### 脚本约定

- 两个脚本都可从任意工作目录通过其实际路径调用。相对的构建目录以项目根目录为基准；`qt-root` 和 `prefix-path` 均以调用目录为基准。
- 首次可用 `./build.sh --configure-only` 或 `./build.ps1 -configure-only` 只生成构建树；后续用 `--skip-configure` / `-skip-configure` 增量构建。跳过配置要求构建目录中已有 `CMakeCache.txt`。
- `configure-only` 不能与 `skip-configure` 或 `run-tests` 同时使用。指定测试目标时，脚本只构建所选目标，不会自动构建筛选条件匹配的其他测试。
- 最终程序输出到项目根目录的 `Debug/` 或 `Release/`，不在 `build/` 中。
- 额外 CMake 选项可通过 `build.sh --cmake-argument '-D...'`（也可放在 `--` 后）或 `build.ps1 -cmake-argument '-D...'` 传入。PowerShell 的稳定接口是显式参数，不提供 `--` 透传。
- `qt-root` 会作为 `CMAKE_PREFIX_PATH` 第一项，之后依次追加 `prefix-path`；Windows 现与 Linux/macOS 使用相同的合并规则。
- 脚本能够解析平台并编排 CMake 命令，不等同于目标平台的依赖缓存和运行时后端已经完成验证。
- 两个平台脚本运行 CTest 时都会传递 `--no-tests=error`；拼错 `test-regex` / `test-label` 并导致零匹配时，脚本会失败。

## 运行测试

推荐由平台脚本完成构建并运行测试：

```bash
# Linux（macOS 需先补齐前述第三方缓存）
./build.sh --run-tests
./build.sh --run-tests --test-label unit
./build.sh --target test_threadmanager --run-tests --test-regex ThreadManagerTest
```

```powershell
# Windows
./build.ps1 -run-tests
./build.ps1 -run-tests -test-label unit
./build.ps1 -target test_threadmanager -run-tests -test-regex ThreadManagerTest
```

也可在已有构建目录中直接调用 CTest：

```bash
cd build
ctest -C Debug --output-on-failure               # 全部测试
ctest -C Debug --output-on-failure --no-tests=error -L unit        # 仅单元测试
ctest -C Debug --output-on-failure --no-tests=error -L integration # 仅集成测试
ctest -C Debug --no-tests=error -R ThreadManagerTest               # 按名称运行
```

当前共有 45 个测试目标定义，其中 `test_nvjpegdecoder` 仅在 Windows 注册：Windows 应注册 45 个测试，Linux/macOS 应注册 44 个；Linux 已实测 44/44 通过。macOS 的数量是补齐缓存后的预期值，当前尚无法执行。

## IDE 配置

### Qt Creator
1. 打开 `CMakeLists.txt` 作为项目
2. Kit 选择 Qt 6.9+ 的 Desktop 配置
3. 构建目录设为 `build/`

### Visual Studio
通过 VS 的「打开 CMake 项目」直接打开 `CMakeLists.txt`

## 常见问题

### Q: 构建报 `Qt6 not found`

优先将 Qt SDK 根目录交给平台脚本：

```bash
# Linux
./build.sh --qt-root /path/to/Qt/6.9.3/gcc_64 --configure-only

# macOS（补齐对应架构缓存后）
./build.sh --qt-root /path/to/Qt/6.9.3/macos --configure-only
```

```powershell
./build.ps1 -qt-root C:\Qt\6.9.3\msvc2022_64 -configure-only
```

直接使用 CMake 时：

```bash
# Linux
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.9.3/gcc_64

# macOS（补齐对应架构缓存后）
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.9.3/macos
```

### Q: Windows PDB 锁定错误
确保使用 VS 生成器。如切换过生成器，删 `build/` 目录重新配置。

### Q: nvJPEG 链接错误
nvJPEG 仅在 Windows 构建中启用，使用 `third_party/nvjpeg/` 内缓存的 CUDA 12.x 文件；缓存不完整时 CMake 会停止并列出缺失文件。Linux 固定使用 CPU TurboJPEG，VA-API 当前显式禁用。macOS 的解码状态见 [平台支持](PLATFORM_SUPPORT.md)。

### Q: 翻译文件未更新
```bash
cmake --build build --target update_translations
```
