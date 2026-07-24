# cmake/SetupNvJpeg.cmake
# ──────────────────────────────────────────────────────────────────────────────
# nvJPEG + CUDA Runtime — 统一从 third_party/nvjpeg/ 预编译缓存读取（已提交 git）
# 缓存缺失时 FATAL_ERROR —— 开发者应通过 CUDA Toolkit 获取后缓存到 third_party/nvjpeg/
#
# 输出: Cuda::cudart, Cuda::nvjpeg (imported, per-config), NVJPEG_TP_BIN
# ──────────────────────────────────────────────────────────────────────────────

include_guard(GLOBAL)

function(rd_setup_nvjpeg)
    if(NOT WIN32)
        return()
    endif()

    set(_TP  "${CMAKE_SOURCE_DIR}/third_party/nvjpeg")
    set(_INC "${_TP}/include")
    set(_LIB "${_TP}/lib/${PLATFORM_NAME}-${PLATFORM_ARCH}")
    set(_BIN "${_TP}/bin/${PLATFORM_NAME}-${PLATFORM_ARCH}")

    # ══════════════════════════════════════════════════════════════════════════
    # 1) 检查预编译缓存是否完整 — 11 头文件 + 2 导入库 + 1 DLL
    # ══════════════════════════════════════════════════════════════════════════
    set(_MISSING "")

    # ── 头文件 ──
    foreach(_hdr
        "nvjpeg.h"
        "cuda_runtime_api.h"
        "crt/host_defines.h"
        "builtin_types.h"
        "device_types.h"
        "driver_types.h"
        "surface_types.h"
        "texture_types.h"
        "vector_types.h"
        "cuda_device_runtime_api.h"
        "library_types.h"
    )
        if(NOT EXISTS "${_INC}/${_hdr}")
            list(APPEND _MISSING "  - 头文件: ${_INC}/${_hdr}")
        endif()
    endforeach()

    # ── 导入库 ──
    if(NOT EXISTS "${_LIB}/cudart.lib")
        list(APPEND _MISSING "  - 导入库: ${_LIB}/cudart.lib")
    endif()
    if(NOT EXISTS "${_LIB}/nvjpeg.lib")
        list(APPEND _MISSING "  - 导入库: ${_LIB}/nvjpeg.lib")
    endif()

    # ── 运行时 DLL ──
    if(NOT EXISTS "${_BIN}/nvjpeg64_12.dll")
        list(APPEND _MISSING "  - DLL: ${_BIN}/nvjpeg64_12.dll")
    endif()

    if(_MISSING)
        string(JOIN "\n" _missing_list ${_MISSING})
        message(FATAL_ERROR
            "[nvJPEG] third_party/nvjpeg/ 预编译缓存不完整，缺少以下文件:\n"
            "${_missing_list}\n\n"
            "  请从 CUDA Toolkit 提取必要文件并缓存到 third_party/nvjpeg/:\n"
            "    - include/     ← 11 个 nvJPEG + CUDA Runtime 头文件\n"
            "    - lib/Windows-x64/  ← cudart.lib + nvjpeg.lib（导入库）\n"
            "    - bin/Windows-x64/  ← nvjpeg64_12.dll（运行时 DLL）")
    endif()

    # ══════════════════════════════════════════════════════════════════════════
    # 2) 创建 imported targets
    # ══════════════════════════════════════════════════════════════════════════
    message(STATUS "[nvJPEG] Using ${_TP}")

    set(NVJPEG_TP_BIN "${_BIN}" PARENT_SCOPE)

    if(NOT TARGET Cuda::cudart)
        add_library(Cuda::cudart UNKNOWN IMPORTED GLOBAL)
        set_target_properties(Cuda::cudart PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES    "${_INC}"
            IMPORTED_LOCATION_DEBUG          "${_LIB}/cudart.lib"
            IMPORTED_LOCATION_RELEASE        "${_LIB}/cudart.lib"
            IMPORTED_LOCATION_RELWITHDEBINFO "${_LIB}/cudart.lib"
            IMPORTED_LOCATION_MINSIZEREL     "${_LIB}/cudart.lib"
        )
    endif()

    if(NOT TARGET Cuda::nvjpeg)
        add_library(Cuda::nvjpeg UNKNOWN IMPORTED GLOBAL)
        set_target_properties(Cuda::nvjpeg PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES    "${_INC}"
            IMPORTED_LOCATION_DEBUG          "${_LIB}/nvjpeg.lib"
            IMPORTED_LOCATION_RELEASE        "${_LIB}/nvjpeg.lib"
            IMPORTED_LOCATION_RELWITHDEBINFO "${_LIB}/nvjpeg.lib"
            IMPORTED_LOCATION_MINSIZEREL     "${_LIB}/nvjpeg.lib"
        )
    endif()
endfunction()
