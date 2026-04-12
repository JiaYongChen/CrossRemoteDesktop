cmake_minimum_required(VERSION 3.16)

project(QtRemoteDesktop VERSION 1.0.0 LANGUAGES CXX C)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sanitizer support (opt-in via -DENABLE_SANITIZERS=address|thread|undefined|memory)
# Usage:  cmake -B build -DENABLE_SANITIZERS=address
#         cmake -B build -DENABLE_SANITIZERS=address,undefined  (comma-separated)
# MSVC supports /fsanitize=address only; GCC/Clang support all four.
option(ENABLE_SANITIZERS "Comma-separated list of sanitizers to enable (address,thread,undefined,memory)" "")
if(ENABLE_SANITIZERS)
    if(MSVC)
        # MSVC only supports AddressSanitizer
        if(ENABLE_SANITIZERS MATCHES "address")
            add_compile_options(/fsanitize=address)
            message(STATUS "[Sanitizers] Enabled: AddressSanitizer (MSVC)")
        else()
            message(WARNING "[Sanitizers] MSVC only supports 'address'; ignoring: ${ENABLE_SANITIZERS}")
        endif()
    else()
        # GCC / Clang
        string(REPLACE "," ";" _sanitizer_list "${ENABLE_SANITIZERS}")
        foreach(_san IN LISTS _sanitizer_list)
            add_compile_options(-fsanitize=${_san})
            add_link_options(-fsanitize=${_san})
            message(STATUS "[Sanitizers] Enabled: ${_san}")
        endforeach()
        # Produce useful stack traces
        add_compile_options(-fno-omit-frame-pointer -g)
    endif()
endif()

# Windows MSVC特定编译器选项
if(MSVC)
    # 防止Windows.h定义min/max宏
    add_compile_definitions(NOMINMAX)
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
    # Qt6要求的__cplusplus宏定义
    add_compile_options(/Zc:__cplusplus)
    # 忽略文件编码警告(C4819)
    add_compile_options(/wd4819)
    # 设置源文件编码为UTF-8
    add_compile_options(/utf-8)
    # 强制同步PDB写入，避免并行编译时PDB文件锁冲突(C1041)
    add_compile_options(/FS)
    # Note: /WX (warnings-as-errors) is set globally later via add_compile_options(/WX)
    # for all project targets. See the "警告即错误" section below.
endif()

# [跨平台Qt6自动检测与配置]
# 说明:
# 1) 自动检测操作系统类型（Windows/Linux/macOS）和处理器架构（x86/x64/ARM）
# 2) 根据平台特性配置Qt6库路径
# 3) 支持多种Qt6安装方式：官方安装器、包管理器（Homebrew/vcpkg/apt等）、手动编译
# 4) 确保架构一致性，避免混合不同架构的库文件

# 检测操作系统和架构信息
if(WIN32)
    set(PLATFORM_NAME "Windows")
    # Windows架构检测
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PLATFORM_ARCH "x64")
    else()
        set(PLATFORM_ARCH "x86")
    endif()
    # 检测ARM64 Windows
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(PLATFORM_ARCH "ARM64")
    endif()
elseif(APPLE)
    set(PLATFORM_NAME "macOS")
    # macOS架构检测
    execute_process(COMMAND uname -m OUTPUT_VARIABLE SYSTEM_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(SYSTEM_ARCH STREQUAL "arm64")
        set(PLATFORM_ARCH "ARM64")
    else()
        set(PLATFORM_ARCH "x64")
    endif()
elseif(UNIX)
    set(PLATFORM_NAME "Linux")
    # Linux架构检测
    execute_process(COMMAND uname -m OUTPUT_VARIABLE SYSTEM_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(SYSTEM_ARCH MATCHES "aarch64|arm64")
        set(PLATFORM_ARCH "ARM64")
    elseif(SYSTEM_ARCH MATCHES "armv7l|armhf")
        set(PLATFORM_ARCH "ARM32")
    elseif(SYSTEM_ARCH MATCHES "x86_64|amd64")
        set(PLATFORM_ARCH "x64")
    else()
        set(PLATFORM_ARCH "x86")
    endif()
else()
    set(PLATFORM_NAME "Unknown")
    set(PLATFORM_ARCH "Unknown")
endif()

message(STATUS "[Qt6 Auto-Detection] Platform: ${PLATFORM_NAME}, Architecture: ${PLATFORM_ARCH}")

# 平台特定的Qt6路径配置
if(APPLE)
    # macOS平台配置
    if(NOT CMAKE_OSX_ARCHITECTURES)
        if(PLATFORM_ARCH STREQUAL "ARM64")
            set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architecture" FORCE)
        else()
            set(CMAKE_OSX_ARCHITECTURES x86_64 CACHE STRING "Target architecture" FORCE)
        endif()
    endif()
    
    # macOS: Homebrew prefix varies by architecture
    if(PLATFORM_ARCH STREQUAL "ARM64")
        set(HOMEBREW_PREFIX "/opt/homebrew")
    else()
        set(HOMEBREW_PREFIX "/usr/local")
    endif()

    # Hint paths for Qt6 (Homebrew, Qt Online Installer)
    list(APPEND CMAKE_PREFIX_PATH
        "${HOMEBREW_PREFIX}/opt/qt@6"
        "${HOMEBREW_PREFIX}/opt/qt"
        "${HOMEBREW_PREFIX}"
    )
    # Glob for Qt Online Installer paths (e.g. ~/Qt/6.9.3/macos)
    file(GLOB _qt_installer_paths "$ENV{HOME}/Qt/*/macos" "/Applications/Qt/*/macos")
    list(APPEND CMAKE_PREFIX_PATH ${_qt_installer_paths})

elseif(WIN32)
    # Hint paths for Qt6 on Windows
    # Environment variables take highest priority
    if(DEFINED ENV{Qt6_DIR})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{Qt6_DIR}")
    endif()
    if(DEFINED ENV{QTDIR})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
    endif()
    # Glob for standard Qt Online Installer paths
    if(PLATFORM_ARCH STREQUAL "ARM64")
        file(GLOB _qt_win_paths "C:/Qt/*/msvc*_arm64")
    elseif(PLATFORM_ARCH STREQUAL "x64")
        file(GLOB _qt_win_paths "C:/Qt/*/msvc*_64" "C:/Qt/*/msvc2022_64")
    else()
        file(GLOB _qt_win_paths "C:/Qt/*/msvc*_32" "C:/Qt/*/msvc*")
    endif()
    list(APPEND CMAKE_PREFIX_PATH ${_qt_win_paths})
    # vcpkg support
    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/${PLATFORM_ARCH}-windows")
    endif()

elseif(UNIX)
    # Hint paths for Qt6 on Linux (system packages + Qt Online Installer)
    list(APPEND CMAKE_PREFIX_PATH
        "/usr/lib/cmake/Qt6"
        "/usr/lib64/cmake/Qt6"
        "/usr/local/lib/cmake/Qt6"
    )
    if(PLATFORM_ARCH STREQUAL "ARM64")
        list(APPEND CMAKE_PREFIX_PATH "/usr/lib/aarch64-linux-gnu/cmake/Qt6")
    elseif(PLATFORM_ARCH STREQUAL "x64")
        list(APPEND CMAKE_PREFIX_PATH "/usr/lib/x86_64-linux-gnu/cmake/Qt6")
    endif()
    file(GLOB _qt_linux_paths "$ENV{HOME}/Qt/*/gcc_64")
    list(APPEND CMAKE_PREFIX_PATH ${_qt_linux_paths})
    if(DEFINED ENV{QTDIR})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
    endif()
endif()

# Let CMake's find_package locate Qt6 via CMAKE_PREFIX_PATH (set above).
# If Qt6_DIR is already set (e.g. via -DQt6_DIR=...), it takes precedence.
# Use QUIET to probe; the definitive REQUIRED find_package with all components comes later.
find_package(Qt6 6.9 QUIET COMPONENTS Core CONFIG)
if(Qt6_FOUND)
    set(QT6_FOUND_PATH "${Qt6_DIR}")
    get_filename_component(QT6_ROOT_DIR "${Qt6_DIR}/../../.." ABSOLUTE)
    message(STATUS "[Qt6 Auto-Detection] Found Qt6 ${Qt6_VERSION} at: ${Qt6_DIR}")
    message(STATUS "[Qt6 Auto-Detection] Qt6 Root: ${QT6_ROOT_DIR}")
else()
    message(WARNING "[Qt6 Auto-Detection] Qt6 >= 6.9 not found. Please set Qt6_DIR or CMAKE_PREFIX_PATH.")
endif()

# 优化CMake查找行为
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF CACHE BOOL "Disable package registry" FORCE)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF CACHE BOOL "Disable system package registry" FORCE)

# 架构一致性验证（确保Qt6库与目标架构匹配）
if(QT6_FOUND_PATH)
    # 检测Qt6库的实际架构
    get_filename_component(QT6_LIB_DIR "${QT6_FOUND_PATH}/../.." ABSOLUTE)
    
    if(APPLE)
        # 在macOS上验证Qt6库架构
        find_file(QT6_CORE_LIB 
            NAMES QtCore QtCore.framework/QtCore
            PATHS "${QT6_LIB_DIR}" "${QT6_LIB_DIR}/QtCore.framework"
            NO_DEFAULT_PATH
        )
        
        if(QT6_CORE_LIB)
            execute_process(
                COMMAND file "${QT6_CORE_LIB}"
                OUTPUT_VARIABLE QT6_LIB_ARCH_INFO
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            
            if(PLATFORM_ARCH STREQUAL "ARM64" AND QT6_LIB_ARCH_INFO MATCHES "x86_64")
                message(WARNING "[Qt6 Auto-Detection] Architecture mismatch: Found x86_64 Qt6 on ARM64 system")
                message(WARNING "[Qt6 Auto-Detection] This may cause linking issues. Consider installing ARM64 Qt6.")
            elseif(PLATFORM_ARCH STREQUAL "x64" AND QT6_LIB_ARCH_INFO MATCHES "arm64")
                message(WARNING "[Qt6 Auto-Detection] Architecture mismatch: Found ARM64 Qt6 on x86_64 system")
                message(WARNING "[Qt6 Auto-Detection] This may cause linking issues. Consider installing x86_64 Qt6.")
            else()
                message(STATUS "[Qt6 Auto-Detection] Architecture verification passed")
            endif()
        endif()
    endif()
    
    # 设置Qt6相关的环境变量（便于调试和工具使用）
    set(ENV{Qt6_DIR} "${Qt6_DIR}")
    set(ENV{QTDIR} "${QT6_ROOT_DIR}")
endif()

# 跨平台编译器和链接器标志优化
if(QT6_FOUND_PATH)
    # Windows特定配置
    if(WIN32)
        # 确保使用正确的运行时库
        if(MSVC)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        endif()
        
        # 设置Windows SDK版本（如果需要）
        if(CMAKE_SYSTEM_VERSION)
            set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "${CMAKE_SYSTEM_VERSION}")
        endif()
    endif()
    
    # Linux特定配置
    if(UNIX AND NOT APPLE)
        # 设置RPATH以便运行时找到Qt6库
        set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
        list(APPEND CMAKE_INSTALL_RPATH "${QT6_ROOT_DIR}/lib")
    endif()
    
    # macOS特定配置
    if(APPLE)
        # 设置macOS部署目标
        if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
            set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "macOS deployment target" FORCE)
        endif()
        
        # 设置RPATH
        set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
        list(APPEND CMAKE_INSTALL_RPATH "${QT6_ROOT_DIR}/lib")
    endif()
endif()

# 最终查找 Qt6 组件
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

# ==============================================================================
# Third-party dependencies (modular setup from cmake/ directory)
# Strategy: check third_party/<lib>/ first, auto-download/build if absent.
# See cmake/SetupOpenSSL.cmake and cmake/SetupZstd.cmake for details.
# ==============================================================================
include(cmake/SetupOpenSSL.cmake)
include(cmake/SetupZstd.cmake)

# 标准Qt项目设置（可用时）
if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
endif()

# 启用Qt的MOC、UIC和RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 开启“警告即错误”的全局编译策略（仅对本项目源文件生效）
# 说明：按用户要求将所有编译 warning 当作 error 处理，避免潜在问题被忽略。
# 注意：此处不强制开启 -Wall/-Wextra，以降低与第三方或历史代码的兼容风险；若仍有警告出现，将以 -Werror 直接中断构建，便于定位和修复。
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    add_compile_options(-Werror)
elseif(MSVC)
    add_compile_options(/WX)
endif()

# 设置UIC搜索路径
set(CMAKE_AUTOUIC_SEARCH_PATHS src/ui)

# 设置输出目录（将 Debug/Release 产物分别放到项目根的 Debug 与 Release 目录）
# 使用生成器表达式根据配置路由到对应文件夹；当配置非 Debug 时，统一归入 Release 文件夹
# 符合定制要求：
#  - 生成的debug调试文件放入Debug文件夹中
#  - 生成的release版本文件放入Release文件夹中
set(_OUT_DIR $<IF:$<CONFIG:Debug>,${CMAKE_SOURCE_DIR}/Debug,${CMAKE_SOURCE_DIR}/Release>)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${_OUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${_OUT_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${_OUT_DIR})

# 头文件（自动检测）
# 使用 GLOB_RECURSE + CONFIGURE_DEPENDS 自动收集 src 目录下所有 .h/.hpp 文件
# 当新增/删除头文件时，CMake 会在下一次配置时自动更新，无需手动维护列表
file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)
set(HEADERS ${PROJECT_HEADERS})

# 源文件（自动检测）
# 使用 GLOB_RECURSE + CONFIGURE_DEPENDS 自动收集 src 目录下所有 .cpp/.cc/.cxx 文件
# 当新增/删除源文件时，CMake 会在下一次配置时自动更新，无需手动维护列表
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
)
set(SOURCES ${PROJECT_SOURCES})

# 生成可执行文件
qt_add_executable(QtRemoteDesktop
    ${SOURCES}
    resources/resources.qrc
)

# 头文件包含目录（为便于使用诸如 "common/core/..." 和 "config/..." 的包含路径）
target_include_directories(QtRemoteDesktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
)

# 链接Qt与其它依赖（仅在Apple平台去除QtGui传播的AGL/OpenGL，保留其它必要框架）
get_target_property(_qtgui_link_libs Qt6::Gui INTERFACE_LINK_LIBRARIES)
if(APPLE AND _qtgui_link_libs)
    set(_filtered_qtgui_link_libs "")
    foreach(_lib IN LISTS _qtgui_link_libs)
        if(_lib MATCHES "(^|[/:;-])AGL($|[/:;-])" OR _lib MATCHES "(^|[/:;-])OpenGL($|[/:;-])")
            # 跳过AGL/OpenGL
        else()
            list(APPEND _filtered_qtgui_link_libs "${_lib}")
        endif()
    endforeach()
    if(NOT _filtered_qtgui_link_libs STREQUAL _qtgui_link_libs)
        set_property(TARGET Qt6::Gui PROPERTY INTERFACE_LINK_LIBRARIES "${_filtered_qtgui_link_libs}")
        message(STATUS "[App] Filtered Qt6::Gui INTERFACE_LINK_LIBRARIES to remove AGL/OpenGL: ${_filtered_qtgui_link_libs}")
    endif()
endif()

# 二次过滤：移除 INTERFACE_LINK_LIBRARIES 中通过 "-framework;AGL/OpenGL" 形式传递的条目
get_target_property(_qtgui_link_libs2 Qt6::Gui INTERFACE_LINK_LIBRARIES)
if(APPLE AND _qtgui_link_libs2)
    set(_filtered_qtgui_link_libs2 "")
    list(LENGTH _qtgui_link_libs2 _len2)
    set(_i2 0)
    while(_i2 LESS _len2)
        list(GET _qtgui_link_libs2 ${_i2} _cur2)
        if(_cur2 STREQUAL "-framework")
            math(EXPR _nx2 "${_i2}+1")
            if(_nx2 LESS _len2)
                list(GET _qtgui_link_libs2 ${_nx2} _fw2)
                if(_fw2 STREQUAL "AGL" OR _fw2 STREQUAL "OpenGL")
                    math(EXPR _i2 "${_i2}+2")
                    continue()
                endif()
            endif()
        endif()
        list(APPEND _filtered_qtgui_link_libs2 "${_cur2}")
        math(EXPR _i2 "${_i2}+1")
    endwhile()
    if(NOT _filtered_qtgui_link_libs2 STREQUAL _qtgui_link_libs2)
        set_property(TARGET Qt6::Gui PROPERTY INTERFACE_LINK_LIBRARIES "${_filtered_qtgui_link_libs2}")
        message(STATUS "[App] Filtered Qt6::Gui INTERFACE_LINK_LIBRARIES to remove -framework AGL/OpenGL (pair form)")
    endif()
endif()

# 继续过滤 Qt6::Gui 的 IMPORTED_LINK_INTERFACE_LIBRARIES（以及各配置特定属性）中的 AGL/OpenGL（部分Qt版本以链接接口库而非链接选项传递）
get_target_property(_qtgui_imp_libs Qt6::Gui IMPORTED_LINK_INTERFACE_LIBRARIES)
if(APPLE AND _qtgui_imp_libs)
    set(_filtered_imp_libs "")
    foreach(_lib IN LISTS _qtgui_imp_libs)
        if(_lib STREQUAL "AGL" OR _lib STREQUAL "OpenGL")
            # 跳过AGL和OpenGL框架
        else()
            list(APPEND _filtered_imp_libs "${_lib}")
        endif()
    endforeach()
    if(NOT _filtered_imp_libs STREQUAL _qtgui_imp_libs)
        set_target_properties(Qt6::Gui PROPERTIES IMPORTED_LINK_INTERFACE_LIBRARIES "${_filtered_imp_libs}")
        message(STATUS "[App] Filtered Qt6::Gui IMPORTED_LINK_INTERFACE_LIBRARIES to remove AGL/OpenGL: ${_filtered_imp_libs}")
    endif()
endif()

# 针对不同构建配置（Debug/Release/RelWithDebInfo/MinSizeRel）进行相同过滤
foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOUPPER "${_cfg}" _CFG_UP)
    get_target_property(_qtgui_imp_libs_${_CFG_UP} Qt6::Gui IMPORTED_LINK_INTERFACE_LIBRARIES_${_CFG_UP})
    if(APPLE AND _qtgui_imp_libs_${_CFG_UP})
        set(_filtered_imp_libs_${_CFG_UP} "")
        foreach(_lib IN LISTS _qtgui_imp_libs_${_CFG_UP})
            if(_lib STREQUAL "AGL" OR _lib STREQUAL "OpenGL")
                # 跳过AGL和OpenGL框架
            else()
                list(APPEND _filtered_imp_libs_${_CFG_UP} "${_lib}")
            endif()
        endforeach()
        if(NOT _filtered_imp_libs_${_CFG_UP} STREQUAL _qtgui_imp_libs_${_CFG_UP})
            set_target_properties(Qt6::Gui PROPERTIES IMPORTED_LINK_INTERFACE_LIBRARIES_${_CFG_UP} "${_filtered_imp_libs_${_CFG_UP}}")
            message(STATUS "[App] Filtered Qt6::Gui IMPORTED_LINK_INTERFACE_LIBRARIES_${_CFG_UP} to remove AGL/OpenGL: ${_filtered_imp_libs_${_CFG_UP}}")
        endif()
    endif()
endforeach()

# 进一步：过滤 WrapOpenGL::WrapOpenGL 的传播内容，避免通过该中间目标引入 AGL/OpenGL 框架
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    # 过滤其 INTERFACE_LINK_LIBRARIES
    get_target_property(_wrap_link_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_wrap_link_libs)
        set(_filtered_wrap_link_libs "")
        foreach(_lib IN LISTS _wrap_link_libs)
            # 过滤名称形式与*.framework路径形式
            if(_lib STREQUAL "AGL" OR _lib STREQUAL "OpenGL" OR _lib MATCHES "(^|/)(AGL|OpenGL)(\\.framework)?(/|$)")
                # 跳过 AGL/OpenGL
            else()
                list(APPEND _filtered_wrap_link_libs "${_lib}")
            endif()
        endforeach()
        if(NOT _filtered_wrap_link_libs STREQUAL _wrap_link_libs)
            set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY INTERFACE_LINK_LIBRARIES "${_filtered_wrap_link_libs}")
            message(STATUS "[App] Filtered WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES to remove AGL/OpenGL: ${_filtered_wrap_link_libs}")
        endif()
        # 二次过滤：移除以 "-framework;AGL/OpenGL" 成对传递的条目
        get_target_property(_wrap_link_libs2 WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
        if(_wrap_link_libs2)
            set(_filtered_wrap_link_libs2 "")
            list(LENGTH _wrap_link_libs2 _wlen)
            set(_wi 0)
            while(_wi LESS _wlen)
                list(GET _wrap_link_libs2 ${_wi} _curw)
                if(_curw STREQUAL "-framework")
                    math(EXPR _wnx "${_wi}+1")
                    if(_wnx LESS _wlen)
                        list(GET _wrap_link_libs2 ${_wnx} _fw)
                        if(_fw STREQUAL "AGL" OR _fw STREQUAL "OpenGL")
                            math(EXPR _wi "${_wi}+2")
                            continue()
                        endif()
                    endif()
                endif()
                list(APPEND _filtered_wrap_link_libs2 "${_curw}")
                math(EXPR _wi "${_wi}+1")
            endwhile()
            if(NOT _filtered_wrap_link_libs2 STREQUAL _wrap_link_libs2)
                set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY INTERFACE_LINK_LIBRARIES "${_filtered_wrap_link_libs2}")
                message(STATUS "[App] Filtered WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES to remove -framework AGL/OpenGL (pair form)")
            endif()
        endif()
    endif()
    
    # 过滤其 INTERFACE_LINK_OPTIONS 中的 -framework 传递
    get_target_property(_wrap_link_opts WrapOpenGL::WrapOpenGL INTERFACE_LINK_OPTIONS)
    if(_wrap_link_opts)
        set(_filtered_wrap_link_opts "")
        list(LENGTH _wrap_link_opts _l2)
        set(_j 0)
        while(_j LESS _l2)
            list(GET _wrap_link_opts ${_j} _cur)
            if(_cur STREQUAL "-framework")
                math(EXPR _nx "${_j}+1")
                if(_nx LESS _l2)
                    list(GET _wrap_link_opts ${_nx} _fw)
                    if(_fw STREQUAL "AGL" OR _fw STREQUAL "OpenGL")
                        math(EXPR _j "${_j}+2")
                        continue()
                    endif()
                endif()
            endif()
            list(APPEND _filtered_wrap_link_opts "${_cur}")
            math(EXPR _j "${_j}+1")
        endwhile()
        if(NOT _filtered_wrap_link_opts STREQUAL _wrap_link_opts)
            set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY INTERFACE_LINK_OPTIONS "${_filtered_wrap_link_opts}")
            message(STATUS "[App] Filtered WrapOpenGL::WrapOpenGL INTERFACE_LINK_OPTIONS to remove -framework AGL/OpenGL")
        endif()
    endif()
endif()

# 先过滤 Qt6::Gui 的 IMPORTED_LINK_OPTIONS（以及各构建配置）中的 -framework AGL/OpenGL
foreach(_prop IN ITEMS IMPORTED_LINK_OPTIONS IMPORTED_LINK_OPTIONS_DEBUG IMPORTED_LINK_OPTIONS_RELEASE IMPORTED_LINK_OPTIONS_RELWITHDEBINFO IMPORTED_LINK_OPTIONS_MINSIZEREL)
    get_target_property(_qtgui_imp_opts Qt6::Gui ${_prop})
    if(APPLE AND _qtgui_imp_opts)
        set(_filtered_qtgui_imp_opts "")
        list(LENGTH _qtgui_imp_opts _len_imp)
        set(_k 0)
        while(_k LESS _len_imp)
            list(GET _qtgui_imp_opts ${_k} _cur_imp)
            if(_cur_imp STREQUAL "-framework")
                math(EXPR _nx_imp "${_k}+1")
                if(_nx_imp LESS _len_imp)
                    list(GET _qtgui_imp_opts ${_nx_imp} _fw_imp)
                    if(_fw_imp STREQUAL "AGL" OR _fw_imp STREQUAL "OpenGL")
                        math(EXPR _k "${_k}+2")
                        continue()
                    endif()
                endif()
            endif()
            list(APPEND _filtered_qtgui_imp_opts "${_cur_imp}")
            math(EXPR _k "${_k}+1")
        endwhile()
        if(NOT _filtered_qtgui_imp_opts STREQUAL _qtgui_imp_opts)
            set_target_properties(Qt6::Gui PROPERTIES ${_prop} "${_filtered_qtgui_imp_opts}")
            message(STATUS "[App] Filtered Qt6::Gui ${_prop} to remove -framework AGL/OpenGL")
        endif()
    endif()
endforeach()

# 同样过滤 WrapOpenGL::WrapOpenGL 的 IMPORTED_LINK_OPTIONS（以及各构建配置）
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    foreach(_prop IN ITEMS IMPORTED_LINK_OPTIONS IMPORTED_LINK_OPTIONS_DEBUG IMPORTED_LINK_OPTIONS_RELEASE IMPORTED_LINK_OPTIONS_RELWITHDEBINFO IMPORTED_LINK_OPTIONS_MINSIZEREL)
        get_target_property(_wrap_imp_opts WrapOpenGL::WrapOpenGL ${_prop})
        if(_wrap_imp_opts)
            set(_filtered_wrap_imp_opts "")
            list(LENGTH _wrap_imp_opts _len_wimp)
            set(_w 0)
            while(_w LESS _len_wimp)
                list(GET _wrap_imp_opts ${_w} _cur_wimp)
                if(_cur_wimp STREQUAL "-framework")
                    math(EXPR _wnx "${_w}+1")
                    if(_wnx LESS _len_wimp)
                        list(GET _wrap_imp_opts ${_wnx} _fw_wimp)
                        if(_fw_wimp STREQUAL "AGL" OR _fw_wimp STREQUAL "OpenGL")
                            math(EXPR _w "${_w}+2")
                            continue()
                        endif()
                    endif()
                endif()
                list(APPEND _filtered_wrap_imp_opts "${_cur_wimp}")
                math(EXPR _w "${_w}+1")
            endwhile()
            if(NOT _filtered_wrap_imp_opts STREQUAL _wrap_imp_opts)
                set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES ${_prop} "${_filtered_wrap_imp_opts}")
                message(STATUS "[App] Filtered WrapOpenGL::WrapOpenGL ${_prop} to remove -framework AGL/OpenGL")
            endif()
        endif()
    endforeach()
endif()

# 继续过滤 Qt6::Gui 的 INTERFACE_LINK_OPTIONS 中的 "-framework AGL/OpenGL"（Homebrew Qt 常以链接选项传递）
get_target_property(_qtgui_link_opts Qt6::Gui INTERFACE_LINK_OPTIONS)
if(APPLE AND _qtgui_link_opts)
    set(_filtered_qtgui_link_opts "")
    list(LENGTH _qtgui_link_opts _len)
    set(_i 0)
    while(_i LESS _len)
        list(GET _qtgui_link_opts ${_i} _cur)
        if(_cur STREQUAL "-framework")
            math(EXPR _next "${_i}+1")
            if(_next LESS _len)
                list(GET _qtgui_link_opts ${_next} _fw)
                if(_fw STREQUAL "AGL" OR _fw STREQUAL "OpenGL")
                    # 跳过该对（-framework AGL/OpenGL）
                    math(EXPR _i "${_i}+2")
                    continue()
                endif()
            endif()
        endif()
        list(APPEND _filtered_qtgui_link_opts "${_cur}")
        math(EXPR _i "${_i}+1")
    endwhile()
    if(NOT _filtered_qtgui_link_opts STREQUAL _qtgui_link_opts)
        set_property(TARGET Qt6::Gui PROPERTY INTERFACE_LINK_OPTIONS "${_filtered_qtgui_link_opts}")
        message(STATUS "[App] Filtered Qt6::Gui INTERFACE_LINK_OPTIONS to remove -framework AGL/OpenGL")
    endif()
endif()

# 添加zstd头文件路径
target_include_directories(QtRemoteDesktop PRIVATE ${ZSTD_INCLUDE_DIR})

# 链接库
target_link_libraries(QtRemoteDesktop PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Gui
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Concurrent
    Qt6::Svg
    OpenSSL::SSL
    OpenSSL::Crypto
    ${ZSTD_LIBRARY}
)
# zstd is pre-built during configure (see cmake/SetupZstd.cmake), no build-time dependency needed

# Windows下自动拷贝OpenSSL运行时DLL到输出目录（TLS所需）
# DLL 位于 OPENSSL_TP_BIN (third_party/openssl/bin/<platform>-<arch>/)
# OPENSSL_TP_BIN 由 cmake/SetupOpenSSL.cmake 设置
if(WIN32 AND EXISTS "${OPENSSL_TP_BIN}/libssl-3-x64.dll")
    add_custom_command(TARGET QtRemoteDesktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENSSL_TP_BIN}/libssl-3-x64.dll"
            "$<TARGET_FILE_DIR:QtRemoteDesktop>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENSSL_TP_BIN}/libcrypto-3-x64.dll"
            "$<TARGET_FILE_DIR:QtRemoteDesktop>"
        COMMENT "Copying OpenSSL DLLs to output directory"
    )
    # 拷贝 Qt TLS 插件到输出目录，确保 Qt 能找到 OpenSSL 后端
    set(QT_TLS_PLUGIN_DIR "${QT6_ROOT_DIR}/plugins/tls")
    if(EXISTS "${QT_TLS_PLUGIN_DIR}")
        add_custom_command(TARGET QtRemoteDesktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:QtRemoteDesktop>/tls"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${QT_TLS_PLUGIN_DIR}"
                "$<TARGET_FILE_DIR:QtRemoteDesktop>/tls"
            COMMENT "Copying Qt TLS plugins to output directory"
        )
    endif()
    # 拷贝 Qt6Svg 运行时 DLL 到输出目录（SVG 插件的核心依赖）
    set(QT_BIN_DIR "${QT6_ROOT_DIR}/bin")
    add_custom_command(TARGET QtRemoteDesktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_BIN_DIR}/$<IF:$<CONFIG:Debug>,Qt6Svgd.dll,Qt6Svg.dll>"
            "$<TARGET_FILE_DIR:QtRemoteDesktop>/"
        COMMENT "Copying Qt6Svg runtime DLL to output directory"
    )
    # 拷贝 Qt SVG 插件到输出目录，确保 QIcon 能正确渲染 SVG 图标
    set(QT_IMAGEFORMATS_DIR "${QT6_ROOT_DIR}/plugins/imageformats")
    set(QT_ICONENGINES_DIR "${QT6_ROOT_DIR}/plugins/iconengines")
    if(EXISTS "${QT_IMAGEFORMATS_DIR}")
        add_custom_command(TARGET QtRemoteDesktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:QtRemoteDesktop>/imageformats"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QT_IMAGEFORMATS_DIR}/$<IF:$<CONFIG:Debug>,qsvgd.dll,qsvg.dll>"
                "$<TARGET_FILE_DIR:QtRemoteDesktop>/imageformats/"
            COMMENT "Copying Qt SVG imageformat plugin to output directory"
        )
    endif()
    if(EXISTS "${QT_ICONENGINES_DIR}")
        add_custom_command(TARGET QtRemoteDesktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:QtRemoteDesktop>/iconengines"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QT_ICONENGINES_DIR}/$<IF:$<CONFIG:Debug>,qsvgicond.dll,qsvgicon.dll>"
                "$<TARGET_FILE_DIR:QtRemoteDesktop>/iconengines/"
            COMMENT "Copying Qt SVG icon engine plugin to output directory"
        )
    endif()
endif()

# 定义宏与编译选项（debug 日志开关、平台宏等）
# 说明：日志输出使用 QLoggingCategory，默认级别为 debug（通过运行时环境变量控制过滤）。
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(QtRemoteDesktop PRIVATE QT_MESSAGELOGCONTEXT)
endif()

# 安装规则（可根据需要扩展）
install(TARGETS QtRemoteDesktop
        RUNTIME DESTINATION bin)

# 启用CTest测试收集（顶层）
# 说明：仅在子目录调用 enable_testing() 会在该子目录生成 CTestTestfile.cmake，
# 顶层未生成则 ctest 无法发现任何测试。这里在顶层启用测试，让ctest能递归收集 test 子目录中的测试。
enable_testing()

# 测试子目录
add_subdirectory(test)