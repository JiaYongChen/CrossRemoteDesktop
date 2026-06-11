cmake_minimum_required(VERSION 3.16)

project(CrossRemoteDesktop VERSION 1.0.0 LANGUAGES CXX C)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Sanitizer support (opt-in via -DENABLE_SANITIZERS=address|thread|undefined|memory)
# ============================================================================
option(ENABLE_SANITIZERS "Comma-separated list of sanitizers to enable (address,thread,undefined,memory)" "")
if(ENABLE_SANITIZERS)
    if(MSVC)
        if(ENABLE_SANITIZERS MATCHES "address")
            add_compile_options(/fsanitize=address)
            message(STATUS "[Sanitizers] Enabled: AddressSanitizer (MSVC)")
        else()
            message(WARNING "[Sanitizers] MSVC only supports 'address'; ignoring: ${ENABLE_SANITIZERS}")
        endif()
    else()
        string(REPLACE "," ";" _sanitizer_list "${ENABLE_SANITIZERS}")
        foreach(_san IN LISTS _sanitizer_list)
            add_compile_options(-fsanitize=${_san})
            add_link_options(-fsanitize=${_san})
            message(STATUS "[Sanitizers] Enabled: ${_san}")
        endforeach()
        add_compile_options(-fno-omit-frame-pointer -g)
    endif()
endif()

# ============================================================================
# MSVC 编译器选项
# ============================================================================
if(MSVC)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
    add_compile_options(/Zc:__cplusplus /wd4819 /utf-8 /FS)
endif()

# ============================================================================
# 平台检测 + Qt6 + 第三方依赖
# ============================================================================
include(cmake/PlatformDetect.cmake)     # → RD_PLATFORM_NAME, RD_PLATFORM_ARCH
include(cmake/SetupOpenSSL.cmake)       # → OpenSSL::SSL, OpenSSL::Crypto
include(cmake/SetupQt6.cmake)           # → Qt6::*, QT6_ROOT_DIR
include(cmake/FilterAppleOpenGL.cmake)  # → rd_filter_apple_opengl()
include(cmake/DeployWindowsDlls.cmake)  # → rd_deploy_windows_runtime()

# 清理 AGL/OpenGL 传播
rd_filter_apple_opengl(Qt6::Gui)
if(TARGET WrapOpenGL::WrapOpenGL)
    rd_filter_apple_opengl(WrapOpenGL::WrapOpenGL)
endif()

# ============================================================================
# Qt 项目标准设置
# ============================================================================
if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS src/ui)

# ============================================================================
# 警告即错误
# ============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    add_compile_options(-Werror)
elseif(MSVC)
    add_compile_options(/WX)
endif()

# ============================================================================
# 输出目录
# ============================================================================
set(_OUT_DIR $<IF:$<CONFIG:Debug>,${CMAKE_SOURCE_DIR}/Debug,${CMAKE_SOURCE_DIR}/Release>)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${_OUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${_OUT_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${_OUT_DIR})

# ============================================================================
# 源文件自动收集
# ============================================================================
file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
)

# ============================================================================
# 可执行目标
# ============================================================================
qt_add_executable(CrossRemoteDesktop WIN32
    ${PROJECT_SOURCES}
    resources/resources.qrc
)

target_include_directories(CrossRemoteDesktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
)

target_link_libraries(CrossRemoteDesktop PRIVATE
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
)

# ── Windows: D3D11 + DXGI + 子系统 + DLL 部署 ──
if(WIN32)
    target_link_libraries(CrossRemoteDesktop PRIVATE d3d11.lib dxgi.lib)
    target_link_options(CrossRemoteDesktop PRIVATE
        $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:/SUBSYSTEM:CONSOLE>
    )
endif()

rd_deploy_windows_runtime(CrossRemoteDesktop)

# ============================================================================
# 编译定义
# ============================================================================
target_compile_definitions(CrossRemoteDesktop PRIVATE
    $<$<CONFIG:Debug>:QT_MESSAGELOGCONTEXT>
)

# ============================================================================
# 安装 + 测试
# ============================================================================
install(TARGETS CrossRemoteDesktop RUNTIME DESTINATION bin)

enable_testing()
add_subdirectory(test)
