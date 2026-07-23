# SetupVideoToolbox.cmake — macOS 屏幕捕获 + GPU 解码
if(NOT APPLE)
    return()
endif()

find_library(AVFOUNDATION AVFoundation REQUIRED)
find_library(COREGRAPHICS CoreGraphics REQUIRED)
find_library(SCREENCAPTUREKIT ScreenCaptureKit)
find_library(VIDEOTOOLBOX VideoToolbox REQUIRED)
find_library(COREVIDEO CoreVideo REQUIRED)
find_library(COREMEDIA CoreMedia REQUIRED)

target_link_libraries(CrossRemoteDesktop PRIVATE
    ${AVFOUNDATION}
    ${COREGRAPHICS}
    ${VIDEOTOOLBOX}
    ${COREVIDEO}
    ${COREMEDIA}
)

if(SCREENCAPTUREKIT)
    target_link_libraries(CrossRemoteDesktop PRIVATE ${SCREENCAPTUREKIT})
    target_compile_definitions(CrossRemoteDesktop PRIVATE HAS_SCREEN_CAPTURE_KIT)
    message(STATUS "ScreenCaptureKit 已找到，启用 macOS 硬件屏幕捕获")
else()
    message(STATUS "ScreenCaptureKit 未找到，macOS 屏幕捕获将使用 Qt 回退")
endif()

message(STATUS "VideoToolbox 已配置，启用 macOS GPU 解码")
target_compile_definitions(CrossRemoteDesktop PRIVATE HAS_VIDEOTOOLBOX)
