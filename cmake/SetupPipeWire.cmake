# SetupPipeWire.cmake — PipeWire 屏幕捕获（Linux 可选组件）
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PIPEWIRE libpipewire-0.3 IMPORTED_TARGET)
endif()

if(PIPEWIRE_FOUND)
    message(STATUS "PipeWire 已找到，启用 Linux 硬件屏幕捕获")
    target_link_libraries(CrossRemoteDesktop PRIVATE PkgConfig::PIPEWIRE)
    target_compile_definitions(CrossRemoteDesktop PRIVATE HAS_PIPEWIRE)
else()
    message(STATUS "PipeWire 未找到，Linux 屏幕捕获将使用 Qt 回退")
endif()
