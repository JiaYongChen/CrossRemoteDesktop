# SetupVideoToolbox.cmake — macOS ScreenCaptureKit + ImageIO 后端
#
# 文件名为历史遗留：JPEG 解码实际使用 ImageIO/CoreGraphics，
# VideoToolbox 不支持该 JPEG 管线，因此不应链接或宣传 VideoToolbox。

include_guard(GLOBAL)

function(_rd_require_macos_target target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "[macOS] 目标尚未创建: ${target_name}")
    endif()
endfunction()

function(_rd_enable_objcxx_arc target_name)
    target_compile_options("${target_name}" PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )
endfunction()

function(rd_setup_macos_image_decoder target_name)
    if(NOT APPLE)
        return()
    endif()

    _rd_require_macos_target("${target_name}")

    find_library(RD_COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(RD_IMAGEIO_FRAMEWORK ImageIO REQUIRED)
    target_link_libraries("${target_name}" PRIVATE
        "${RD_COREGRAPHICS_FRAMEWORK}"
        "${RD_IMAGEIO_FRAMEWORK}"
    )
    target_compile_definitions("${target_name}" PRIVATE HAS_MACOS_IMAGEIO)
    _rd_enable_objcxx_arc("${target_name}")
endfunction()

function(rd_setup_macos_screen_capture target_name)
    if(NOT APPLE)
        return()
    endif()

    _rd_require_macos_target("${target_name}")
    find_library(RD_SCREENCAPTUREKIT_FRAMEWORK ScreenCaptureKit)
    if(RD_SCREENCAPTUREKIT_FRAMEWORK)
        find_library(RD_COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
        find_library(RD_COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
        find_library(RD_COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
        target_link_libraries("${target_name}" PRIVATE
            "${RD_SCREENCAPTUREKIT_FRAMEWORK}"
            "${RD_COREGRAPHICS_FRAMEWORK}"
            "${RD_COREMEDIA_FRAMEWORK}"
            "${RD_COREVIDEO_FRAMEWORK}"
        )
        target_compile_definitions("${target_name}" PRIVATE HAS_SCREEN_CAPTURE_KIT)
        _rd_enable_objcxx_arc("${target_name}")
        message(STATUS "[macOS] 已启用 ScreenCaptureKit 捕获（运行时要求 macOS 13+）")
    else()
        message(STATUS "[macOS] 未找到 ScreenCaptureKit，捕获回退 Qt QScreen")
    endif()
endfunction()

function(rd_setup_macos_backends target_name)
    if(NOT APPLE)
        return()
    endif()

    rd_setup_macos_image_decoder("${target_name}")
    rd_setup_macos_screen_capture("${target_name}")
    message(STATUS "[macOS] 已启用 ImageIO/CoreGraphics JPEG 解码")
endfunction()
