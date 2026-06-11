# cmake/SetupQt6.cmake
# Qt6 自动检测: 路径搜索 + 架构验证 + find_package
#
# 依赖: cmake/PlatformDetect.cmake（需先 include）
# 输出: Qt6::* 导入目标, QT6_ROOT_DIR 变量

message(STATUS "[Qt6 Setup] Platform: ${RD_PLATFORM_NAME}, Architecture: ${RD_PLATFORM_ARCH}")

# ── macOS 平台 Qt6 路径 ──
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        if(RD_PLATFORM_ARCH STREQUAL "ARM64")
            set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architecture" FORCE)
        else()
            set(CMAKE_OSX_ARCHITECTURES x86_64 CACHE STRING "Target architecture" FORCE)
        endif()
    endif()

    if(RD_PLATFORM_ARCH STREQUAL "ARM64")
        set(_homebrew_prefix "/opt/homebrew")
    else()
        set(_homebrew_prefix "/usr/local")
    endif()

    list(APPEND CMAKE_PREFIX_PATH
        "${_homebrew_prefix}/opt/qt@6"
        "${_homebrew_prefix}/opt/qt"
        "${_homebrew_prefix}"
    )
    file(GLOB _qt_installer_paths "$ENV{HOME}/Qt/*/macos" "/Applications/Qt/*/macos")
    list(APPEND CMAKE_PREFIX_PATH ${_qt_installer_paths})

# ── Windows 平台 Qt6 路径 ──
elseif(WIN32)
    if(DEFINED ENV{Qt6_DIR})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{Qt6_DIR}")
    endif()
    if(DEFINED ENV{QTDIR})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
    endif()
    if(RD_PLATFORM_ARCH STREQUAL "ARM64")
        file(GLOB _qt_win_paths "C:/Qt/*/msvc*_arm64")
    elseif(RD_PLATFORM_ARCH STREQUAL "x64")
        file(GLOB _qt_win_paths "C:/Qt/*/msvc*_64" "C:/Qt/*/msvc2022_64")
    else()
        file(GLOB _qt_win_paths "C:/Qt/*/msvc*_32" "C:/Qt/*/msvc*")
    endif()
    list(APPEND CMAKE_PREFIX_PATH ${_qt_win_paths})
    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/${RD_PLATFORM_ARCH}-windows")
    endif()

# ── Linux 平台 Qt6 路径 ──
elseif(UNIX)
    list(APPEND CMAKE_PREFIX_PATH
        "/usr/lib/cmake/Qt6"
        "/usr/lib64/cmake/Qt6"
        "/usr/local/lib/cmake/Qt6"
    )
    if(RD_PLATFORM_ARCH STREQUAL "ARM64")
        list(APPEND CMAKE_PREFIX_PATH "/usr/lib/aarch64-linux-gnu/cmake/Qt6")
    elseif(RD_PLATFORM_ARCH STREQUAL "x64")
        list(APPEND CMAKE_PREFIX_PATH "/usr/lib/x86_64-linux-gnu/cmake/Qt6")
    endif()
    file(GLOB _qt_linux_paths "$ENV{HOME}/Qt/*/gcc_64")
    list(APPEND CMAKE_PREFIX_PATH ${_qt_linux_paths})
    if(DEFINED ENV{QTDIR})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
    endif()
endif()

# 禁用包注册表加速查找
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF CACHE BOOL "Disable package registry" FORCE)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF CACHE BOOL "Disable system package registry" FORCE)

# ── 探测 Qt6 ──
find_package(Qt6 6.9 QUIET COMPONENTS Core CONFIG)
if(Qt6_FOUND)
    set(QT6_FOUND_PATH "${Qt6_DIR}")
    get_filename_component(QT6_ROOT_DIR "${Qt6_DIR}/../../.." ABSOLUTE)
    message(STATUS "[Qt6 Setup] Found Qt6 ${Qt6_VERSION} at: ${Qt6_DIR}")
    message(STATUS "[Qt6 Setup] Qt6 Root: ${QT6_ROOT_DIR}")
else()
    message(WARNING "[Qt6 Setup] Qt6 >= 6.9 not found. Please set Qt6_DIR or CMAKE_PREFIX_PATH.")
endif()

# ── 架构一致性验证（macOS）──
if(QT6_FOUND_PATH AND APPLE)
    get_filename_component(_qt6_lib_dir "${QT6_FOUND_PATH}/../.." ABSOLUTE)
    find_file(_qt6_core_lib
        NAMES QtCore QtCore.framework/QtCore
        PATHS "${_qt6_lib_dir}" "${_qt6_lib_dir}/QtCore.framework"
        NO_DEFAULT_PATH
    )
    if(_qt6_core_lib)
        execute_process(
            COMMAND file "${_qt6_core_lib}"
            OUTPUT_VARIABLE _qt6_lib_arch_info
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(RD_PLATFORM_ARCH STREQUAL "ARM64" AND _qt6_lib_arch_info MATCHES "x86_64")
            message(WARNING "[Qt6 Setup] Architecture mismatch: Found x86_64 Qt6 on ARM64 system")
        elseif(RD_PLATFORM_ARCH STREQUAL "x64" AND _qt6_lib_arch_info MATCHES "arm64")
            message(WARNING "[Qt6 Setup] Architecture mismatch: Found ARM64 Qt6 on x86_64 system")
        else()
            message(STATUS "[Qt6 Setup] Architecture verification passed")
        endif()
    endif()
endif()

# ── 设置环境变量 ──
if(QT6_FOUND_PATH)
    set(ENV{Qt6_DIR} "${Qt6_DIR}")
    set(ENV{QTDIR} "${QT6_ROOT_DIR}")
endif()

# ── 平台特定配置 ──
if(QT6_FOUND_PATH)
    if(WIN32 AND MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    if(UNIX AND NOT APPLE)
        set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
        list(APPEND CMAKE_INSTALL_RPATH "${QT6_ROOT_DIR}/lib")
    endif()
    if(APPLE)
        if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
            set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "macOS deployment target" FORCE)
        endif()
        set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
        list(APPEND CMAKE_INSTALL_RPATH "${QT6_ROOT_DIR}/lib")
    endif()
endif()

# ── 最终查找全部 Qt6 组件 ──
find_package(Qt6 6.9 REQUIRED COMPONENTS
    Core
    Widgets
    Network
    Gui
    OpenGL
    OpenGLWidgets
    Concurrent
    Svg
)
