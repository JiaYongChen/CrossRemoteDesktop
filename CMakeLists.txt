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
include(cmake/SetupLibJpegTurbo.cmake)  # → LibJpegTurbo::turbojpeg
include(cmake/SetupQt6.cmake)           # → Qt6::*, QT6_ROOT_DIR
include(cmake/FilterAppleOpenGL.cmake)  # → rd_filter_apple_opengl()
include(cmake/SetupNvJpeg.cmake)        # → rd_setup_nvjpeg(), Cuda::cudart, Cuda::nvjpeg
include(cmake/SetupPipeWire.cmake)      # → PkgConfig::PIPEWIRE, HAS_PIPEWIRE (Linux)
include(cmake/SetupVideoToolbox.cmake)  # → AVFoundation, VideoToolbox, HAS_SCREEN_CAPTURE_KIT, HAS_VIDEOTOOLBOX (macOS)
include(cmake/SetupVaApi.cmake)         # → PkgConfig::LIBVA, PkgConfig::LIBVA_DRM, HAS_VAAPI (Linux)
include(cmake/DeployWindowsDlls.cmake)  # → rd_deploy_windows_runtime()

rd_setup_nvjpeg()

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
    add_compile_options(-Wmisleading-indentation)
elseif(MSVC)
    add_compile_options(/WX)
    add_compile_options(/w34768)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${TURBOJPEG_INCLUDE_DIR}
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
    LibJpegTurbo::turbojpeg
)

# ── CUDA + nvJPEG（编译时直接链接，运行时 DLL 必须存在）──
if(CUDA_AVAILABLE AND NVJPEG_AVAILABLE)
    target_link_libraries(CrossRemoteDesktop PRIVATE
        Cuda::cudart Cuda::nvjpeg)
    target_compile_definitions(CrossRemoteDesktop PRIVATE HAS_NVJPEG)
    message(STATUS "[CUDA] Linked — GPU decode available")
endif()

# ── Windows: D3D11 + DXGI + 子系统 + DLL 部署 ──
if(WIN32)
    target_link_libraries(CrossRemoteDesktop PRIVATE d3d11.lib dxgi.lib dwmapi.lib)
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

# ============================================================================
# 翻译更新目标（lupdate）
# ============================================================================
# 源码变更后运行，自动扫描 tr() 调用并同步 TS 文件的行号与条目：
#   cmake --build build --target update_translations
# ============================================================================
file(GLOB_RECURSE TRANSLATABLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.ui"
)

set(TS_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/translations/zh_CN.ts"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/translations/en_US.ts"
)

find_program(LUPDATE_EXECUTABLE lupdate
    HINTS "${QT6_ROOT_DIR}/bin"
    DOC "Path to Qt lupdate tool"
)

if(LUPDATE_EXECUTABLE)
    add_custom_target(update_translations
        COMMAND ${LUPDATE_EXECUTABLE} ${TRANSLATABLE_SOURCES} -ts ${TS_FILES} -no-obsolete
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Syncing TS files with source tr() calls..."
    )

    find_program(LRELEASE_EXECUTABLE lrelease
        HINTS "${QT6_ROOT_DIR}/bin"
        DOC "Path to Qt lrelease tool"
    )
    if(LRELEASE_EXECUTABLE)
        # 不带 -qm：lrelease 为每个 .ts 在同目录生成同名 .qm（TS 本就位于 resources/translations/；
        # -qm 只接受单个输出文件名，多 TS + 目录参数会导致 "cannot create" 失败）
        add_custom_target(release_translations
            COMMAND ${LRELEASE_EXECUTABLE} ${TS_FILES}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Compiling TS → QM translation files..."
        )
        # 组合目标：lupdate → lrelease 一键完成
        add_custom_target(sync_translations
            COMMAND ${LUPDATE_EXECUTABLE} ${TRANSLATABLE_SOURCES} -ts ${TS_FILES} -no-obsolete
            COMMAND ${LRELEASE_EXECUTABLE} ${TS_FILES}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Running lupdate + lrelease: syncing and compiling translations..."
        )
        message(STATUS "[Translation] lupdate + lrelease configured")
    endif()
endif()

enable_testing()
add_subdirectory(test)
