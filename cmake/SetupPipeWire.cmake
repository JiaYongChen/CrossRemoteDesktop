# SetupPipeWire.cmake — PipeWire 屏幕捕获依赖探测（Linux）
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PIPEWIRE libpipewire-0.3 IMPORTED_TARGET)
endif()

if(PIPEWIRE_FOUND)
    # PipeWire 本身不提供安全的桌面流发现机制。Wayland 屏幕捕获必须先经
    # xdg-desktop-portal 完成用户授权，并把 portal 返回的 remote fd 和
    # stream node id 交给 PipeWire。该流程尚未接入前，不能仅因开发包存在
    # 就把后端标记为可用，否则会选择一个无法获得屏幕流的实现。
    message(STATUS
        "PipeWire 已找到，但 xdg-desktop-portal 捕获流程尚未接入；"
        "保持禁用并使用 Qt QScreen 回退")
else()
    message(STATUS "PipeWire 未找到，Linux 屏幕捕获将使用 Qt 回退")
endif()
