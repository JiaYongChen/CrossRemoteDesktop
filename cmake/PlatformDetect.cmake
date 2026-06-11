# cmake/PlatformDetect.cmake
# 跨平台 OS/架构自动检测
#
# 输出变量:
#   RD_PLATFORM_NAME  — "Windows" / "macOS" / "Linux"
#   RD_PLATFORM_ARCH  — "x64" / "ARM64" / "x86" / "ARM32"
#   PLATFORM_NAME     — 兼容别名（SetupOpenSSL.cmake 使用）
#   PLATFORM_ARCH     — 兼容别名（SetupOpenSSL.cmake 使用）

# 检测操作系统和架构信息
if(WIN32)
    set(RD_PLATFORM_NAME "Windows")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(RD_PLATFORM_ARCH "x64")
    else()
        set(RD_PLATFORM_ARCH "x86")
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(RD_PLATFORM_ARCH "ARM64")
    endif()
elseif(APPLE)
    set(RD_PLATFORM_NAME "macOS")
    execute_process(COMMAND uname -m OUTPUT_VARIABLE _sys_arch OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_sys_arch STREQUAL "arm64")
        set(RD_PLATFORM_ARCH "ARM64")
    else()
        set(RD_PLATFORM_ARCH "x64")
    endif()
elseif(UNIX)
    set(RD_PLATFORM_NAME "Linux")
    execute_process(COMMAND uname -m OUTPUT_VARIABLE _sys_arch OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_sys_arch MATCHES "aarch64|arm64")
        set(RD_PLATFORM_ARCH "ARM64")
    elseif(_sys_arch MATCHES "armv7l|armhf")
        set(RD_PLATFORM_ARCH "ARM32")
    elseif(_sys_arch MATCHES "x86_64|amd64")
        set(RD_PLATFORM_ARCH "x64")
    else()
        set(RD_PLATFORM_ARCH "x86")
    endif()
else()
    set(RD_PLATFORM_NAME "Unknown")
    set(RD_PLATFORM_ARCH "Unknown")
endif()

# 向后兼容别名（SetupOpenSSL.cmake 等已有代码使用）
set(PLATFORM_NAME "${RD_PLATFORM_NAME}")
set(PLATFORM_ARCH "${RD_PLATFORM_ARCH}")

message(STATUS "[Platform] Detected: ${RD_PLATFORM_NAME}, Architecture: ${RD_PLATFORM_ARCH}")
