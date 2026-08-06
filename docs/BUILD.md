# 构建指南

## 依赖

| 依赖 | 版本要求 | 获取方式 |
|------|---------|---------|
| Qt | 6.9+ | [qt.io](https://www.qt.io/download) |
| OpenSSL | 1.1+ | vcpkg / 系统包管理器 |
| libjpeg-turbo | 2.0+ | vcpkg / 系统包管理器 |
| CUDA Toolkit | 12.x (可选) | [NVIDIA](https://developer.nvidia.com/cuda-downloads) |
| CMake | 3.16+ | [cmake.org](https://cmake.org/download/) |
| C++ 编译器 | C++20 支持 | MSVC 2022+ / GCC 13+ / Clang 16+ |

## 获取源码

```bash
git clone <repo-url>
cd CrossRemoteDesktop
```

## 第三方库

项目使用 [vcpkg](https://github.com/microsoft/vcpkg) 获取预编译依赖，产物缓存于 `third_party/` 并提交 git。CMake 构建时直接使用缓存，不调用 vcpkg。

开发者首次设置：运行 `.claude/scripts/setup-symlinks.bat`（Windows）或 `setup-symlinks.sh`（macOS/Linux）。

## 构建

### Windows

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

必须使用 VS 生成器（非 Ninja），需要 `/FS` 避免 PDB 锁冲突。

### macOS / Linux

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

## 运行测试

```bash
cd build
ctest -C Debug --output-on-failure              # 全部测试
ctest -C Debug --output-on-failure -L unit       # 仅单元测试
ctest -C Debug --output-on-failure -L integration # 仅集成测试
ctest -C Debug -R ThreadManagerTest              # 按名称运行
```

## IDE 配置

### Qt Creator
1. 打开 `CMakeLists.txt` 作为项目
2. Kit 选择 Qt 6.9+ 的 Desktop 配置
3. 构建目录设为 `build/`

### Visual Studio
通过 VS 的「打开 CMake 项目」直接打开 `CMakeLists.txt`

## 常见问题

### Q: 构建报 `Qt6 not found`
```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.9.0/gcc_64
```

### Q: Windows PDB 锁定错误
确保使用 VS 生成器。如切换过生成器，删 `build/` 目录重新配置。

### Q: nvJPEG 链接错误
nvJPEG 可选。未装 CUDA Toolkit 时构建自动跳过，运行时回退 CPU TurboJPEG。

### Q: 翻译文件未更新
```bash
cmake --build build --target update_translations
```
