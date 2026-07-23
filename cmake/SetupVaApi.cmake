# SetupVaApi.cmake — VA-API GPU JPEG 解码（Linux 可选组件）
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBVA libva IMPORTED_TARGET)
    pkg_check_modules(LIBVA_DRM libva-drm IMPORTED_TARGET)
endif()

if(LIBVA_FOUND AND LIBVA_DRM_FOUND)
    message(STATUS "VA-API 已找到，启用 Linux GPU JPEG 解码")
    target_link_libraries(CrossRemoteDesktop PRIVATE
        PkgConfig::LIBVA
        PkgConfig::LIBVA_DRM
    )
    target_compile_definitions(CrossRemoteDesktop PRIVATE HAS_VAAPI)
else()
    message(STATUS "VA-API 未找到，Linux JPEG 解码使用 CPU 回退")
endif()
