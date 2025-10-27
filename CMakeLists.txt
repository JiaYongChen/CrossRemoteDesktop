cmake_minimum_required(VERSION 3.16)

project(QtRemoteDesktop VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# [跨平台Qt6自动检测与配置]
# 说明：
# 1) 自动检测操作系统类型（Windows/Linux/macOS）和处理器架构（x86/x64/ARM）
# 2) 根据平台特性配置Qt6库路径、OpenSSL路径等依赖项
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
    
    # Qt6路径检测优先级（按常见安装位置排序）
    set(QT6_SEARCH_PATHS "")
    
    if(PLATFORM_ARCH STREQUAL "ARM64")
        # Apple Silicon Mac优先路径
        list(APPEND QT6_SEARCH_PATHS
            "/opt/homebrew/opt/qt@6/lib/cmake/Qt6"
            "/opt/homebrew/opt/qt/lib/cmake/Qt6"
            "/opt/homebrew/lib/cmake/Qt6"
            "/usr/local/opt/qt@6/lib/cmake/Qt6"  # 备用路径
            "/usr/local/opt/qt/lib/cmake/Qt6"
            "/Applications/Qt/*/macos/lib/cmake/Qt6"
            "~/Qt/*/macos/lib/cmake/Qt6"
        )
        set(HOMEBREW_PREFIX "/opt/homebrew")
        set(OPENSSL_SEARCH_PATH "/opt/homebrew/opt/openssl@3")
    else()
        # Intel Mac优先路径
        list(APPEND QT6_SEARCH_PATHS
            "/usr/local/opt/qt@6/lib/cmake/Qt6"
            "/usr/local/opt/qt/lib/cmake/Qt6"
            "/usr/local/lib/cmake/Qt6"
            "/opt/homebrew/opt/qt@6/lib/cmake/Qt6"  # 备用路径
            "/opt/homebrew/opt/qt/lib/cmake/Qt6"
            "/Applications/Qt/*/macos/lib/cmake/Qt6"
            "~/Qt/*/macos/lib/cmake/Qt6"
        )
        set(HOMEBREW_PREFIX "/usr/local")
        set(OPENSSL_SEARCH_PATH "/usr/local/opt/openssl@3")
    endif()
    
elseif(WIN32)
    # Windows平台配置
    set(QT6_SEARCH_PATHS "")
    
    # 检测常见的Qt6安装路径
    if(PLATFORM_ARCH STREQUAL "x64")
        list(APPEND QT6_SEARCH_PATHS
            "C:/Qt/*/msvc*/lib/cmake/Qt6"
            "C:/Qt/*/msvc*_64/lib/cmake/Qt6"
            "C:/Program Files/Qt/*/lib/cmake/Qt6"
            "$ENV{QTDIR}/lib/cmake/Qt6"
            "$ENV{Qt6_DIR}"
        )
    elseif(PLATFORM_ARCH STREQUAL "ARM64")
        list(APPEND QT6_SEARCH_PATHS
            "C:/Qt/*/msvc*_arm64/lib/cmake/Qt6"
            "C:/Program Files/Qt/*/lib/cmake/Qt6"
            "$ENV{QTDIR}/lib/cmake/Qt6"
            "$ENV{Qt6_DIR}"
        )
    else()
        list(APPEND QT6_SEARCH_PATHS
            "C:/Qt/*/msvc*/lib/cmake/Qt6"
            "C:/Qt/*/msvc*_32/lib/cmake/Qt6"
            "C:/Program Files (x86)/Qt/*/lib/cmake/Qt6"
            "$ENV{QTDIR}/lib/cmake/Qt6"
            "$ENV{Qt6_DIR}"
        )
    endif()
    
    # vcpkg支持
    if(DEFINED ENV{VCPKG_ROOT})
        if(PLATFORM_ARCH STREQUAL "x64")
            list(APPEND QT6_SEARCH_PATHS "$ENV{VCPKG_ROOT}/installed/x64-windows/share/qt6")
        elseif(PLATFORM_ARCH STREQUAL "ARM64")
            list(APPEND QT6_SEARCH_PATHS "$ENV{VCPKG_ROOT}/installed/arm64-windows/share/qt6")
        else()
            list(APPEND QT6_SEARCH_PATHS "$ENV{VCPKG_ROOT}/installed/x86-windows/share/qt6")
        endif()
    endif()
    
elseif(UNIX)
    # Linux平台配置
    set(QT6_SEARCH_PATHS "")
    
    # 检测常见的Qt6安装路径
    list(APPEND QT6_SEARCH_PATHS
        "/usr/lib/cmake/Qt6"
        "/usr/lib64/cmake/Qt6"
        "/usr/local/lib/cmake/Qt6"
        "/usr/local/lib64/cmake/Qt6"
        "/opt/qt*/lib/cmake/Qt6"
        "~/Qt/*/gcc_64/lib/cmake/Qt6"
        "$ENV{QTDIR}/lib/cmake/Qt6"
        "$ENV{Qt6_DIR}"
    )
    
    # 架构特定路径
    if(PLATFORM_ARCH STREQUAL "ARM64")
        list(APPEND QT6_SEARCH_PATHS
            "/usr/lib/aarch64-linux-gnu/cmake/Qt6"
            "~/Qt/*/gcc_arm64/lib/cmake/Qt6"
        )
    elseif(PLATFORM_ARCH STREQUAL "ARM32")
        list(APPEND QT6_SEARCH_PATHS
            "/usr/lib/arm-linux-gnueabihf/cmake/Qt6"
            "~/Qt/*/gcc_armhf/lib/cmake/Qt6"
        )
    else()
        list(APPEND QT6_SEARCH_PATHS
            "/usr/lib/x86_64-linux-gnu/cmake/Qt6"
        )
    endif()
endif()

# 自动检测Qt6安装路径
set(QT6_FOUND_PATH "")
foreach(QT6_PATH ${QT6_SEARCH_PATHS})
    # 展开通配符路径
    file(GLOB QT6_EXPANDED_PATHS "${QT6_PATH}")
    foreach(QT6_EXPANDED_PATH ${QT6_EXPANDED_PATHS})
        if(EXISTS "${QT6_EXPANDED_PATH}/Qt6Config.cmake")
            set(QT6_FOUND_PATH "${QT6_EXPANDED_PATH}")
            break()
        endif()
    endforeach()
    if(QT6_FOUND_PATH)
        break()
    endif()
endforeach()

# 配置Qt6路径
if(QT6_FOUND_PATH)
    set(Qt6_DIR "${QT6_FOUND_PATH}" CACHE PATH "Qt6 Config path (Auto-detected)" FORCE)
    get_filename_component(QT6_ROOT_DIR "${QT6_FOUND_PATH}/../../.." ABSOLUTE)
    
    # 设置CMAKE_PREFIX_PATH
    if(APPLE AND HOMEBREW_PREFIX)
        set(CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX};${QT6_ROOT_DIR};${HOMEBREW_PREFIX}/opt/qt" CACHE STRING "Auto-detected prefixes" FORCE)
    else()
        set(CMAKE_PREFIX_PATH "${QT6_ROOT_DIR}" CACHE STRING "Auto-detected Qt6 prefix" FORCE)
    endif()
    
    message(STATUS "[Qt6 Auto-Detection] Found Qt6 at: ${QT6_FOUND_PATH}")
    message(STATUS "[Qt6 Auto-Detection] Qt6 Root: ${QT6_ROOT_DIR}")
else()
    message(WARNING "[Qt6 Auto-Detection] Qt6 not found in standard locations. Please set Qt6_DIR manually.")
endif()

# OpenSSL路径配置（平台特定）
if(APPLE AND OPENSSL_SEARCH_PATH AND EXISTS "${OPENSSL_SEARCH_PATH}")
    set(OPENSSL_ROOT_DIR "${OPENSSL_SEARCH_PATH}" CACHE PATH "OpenSSL root (Auto-detected)" FORCE)
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_SEARCH_PATH}/include" CACHE PATH "OpenSSL include (Auto-detected)" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_SEARCH_PATH}/lib/libcrypto.dylib" CACHE FILEPATH "OpenSSL crypto lib (Auto-detected)" FORCE)
    set(OPENSSL_SSL_LIBRARY "${OPENSSL_SEARCH_PATH}/lib/libssl.dylib" CACHE FILEPATH "OpenSSL ssl lib (Auto-detected)" FORCE)
    message(STATUS "[OpenSSL Auto-Detection] Found OpenSSL at: ${OPENSSL_SEARCH_PATH}")
elseif(WIN32)
    # Windows OpenSSL检测
    if(DEFINED ENV{VCPKG_ROOT})
        if(PLATFORM_ARCH STREQUAL "x64")
            set(OPENSSL_ROOT_DIR "$ENV{VCPKG_ROOT}/installed/x64-windows" CACHE PATH "OpenSSL root (vcpkg)" FORCE)
        elseif(PLATFORM_ARCH STREQUAL "ARM64")
            set(OPENSSL_ROOT_DIR "$ENV{VCPKG_ROOT}/installed/arm64-windows" CACHE PATH "OpenSSL root (vcpkg)" FORCE)
        else()
            set(OPENSSL_ROOT_DIR "$ENV{VCPKG_ROOT}/installed/x86-windows" CACHE PATH "OpenSSL root (vcpkg)" FORCE)
        endif()
    endif()
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
    Multimedia
    Gui
    OpenGL
    OpenGLWidgets
    Test
    Concurrent
)

# 新增：查找第三方依赖（用于加解密和PNG编码）
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

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

# 链接库
target_link_libraries(QtRemoteDesktop PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::Gui
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Test
    Qt6::Concurrent
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
)

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