# cmake/SetupNvJpeg.cmake
# ──────────────────────────────────────────────────────────────────────────
# nvJPEG GPU JPEG 库（编译时链接）
#
# 三级机制:
#  1. 检测 third_party/nvjpeg/ 预缓存文件 → 直接用（团队/CI 离线）
#  2. 从 CUDA Toolkit 安装目录复制 → 缓存到 third_party/nvjpeg/
#  3. 找不到 → 跳过（应用运行时自动降级到 CPU 解码）
#
# 输出:
#   NvJpeg::nvjpeg  — nvJPEG 导入库目标
#   NvJpeg::cudart  — CUDA Runtime 导入库目标
#   NVJPEG_INCLUDE_DIR / NVJPEG_TP_BIN
# ──────────────────────────────────────────────────────────────────────────

include_guard(GLOBAL)

function(rd_setup_nvjpeg)
    if(NOT WIN32)
        message(STATUS "[nvJPEG] Skipped — only Windows is supported")
        return()
    endif()

    set(_NJP_TP  "${CMAKE_SOURCE_DIR}/third_party/nvjpeg")
    set(_NJP_INC "${_NJP_TP}/include")
    set(_NJP_LIB "${_NJP_TP}/lib/Windows-x64")
    set(_NJP_BIN "${_NJP_TP}/bin/Windows-x64")

    # ── Tier 1: third_party 缓存 ─────────────────────────────────────────
    if(EXISTS "${_NJP_LIB}/nvjpeg.lib" AND EXISTS "${_NJP_LIB}/cudart.lib"
       AND EXISTS "${_NJP_INC}/nvjpeg.h" AND EXISTS "${_NJP_INC}/cuda_runtime_api.h")
        message(STATUS "[nvJPEG] Using cached SDK from third_party/nvjpeg/ (CUDA 12.x)")
        _rd_create_nvjpeg_targets("${_NJP_INC}" "${_NJP_LIB}" "${_NJP_BIN}")
        return()
    endif()

    # ── Tier 2: CUDA Toolkit 安装 ────────────────────────────────────────
    set(_CUDA "")
    if(DEFINED ENV{CUDA_PATH})
        set(_CUDA "$ENV{CUDA_PATH}")
    else()
        file(GLOB _CUDA_DIRS "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v*")
        if(_CUDA_DIRS)
            list(SORT _CUDA_DIRS COMPARE NATURAL ORDER DESCENDING)
            list(GET _CUDA_DIRS 0 _CUDA)
        endif()
    endif()

    if(_CUDA AND EXISTS "${_CUDA}/include/nvjpeg.h")
        message(STATUS "[nvJPEG] Found CUDA Toolkit at ${_CUDA}")

        file(MAKE_DIRECTORY "${_NJP_INC}" "${_NJP_LIB}" "${_NJP_BIN}")

        # 复制头文件
        file(COPY "${_CUDA}/include/nvjpeg.h" DESTINATION "${_NJP_INC}")
        file(COPY "${_CUDA}/include/" DESTINATION "${_NJP_INC}"
             FILES_MATCHING PATTERN "cuda*.h" PATTERN "*.hpp"
             PATTERN "builtin_types.h" PATTERN "driver_types.h"
             PATTERN "vector_types.h" PATTERN "surface_types.h"
             PATTERN "texture_types.h" PATTERN "library_types.h"
             PATTERN "device_types.h" PATTERN "host_defines.h"
             PATTERN "host_config.h" PATTERN "channel_descriptor.h"
             PATTERN "common_functions.h" PATTERN "math_constants.h"
             PATTERN "crt/*" PATTERN "crt/host_defines.h")

        # 复制导入库
        if(EXISTS "${_CUDA}/lib/x64/nvjpeg.lib")
            file(COPY "${_CUDA}/lib/x64/nvjpeg.lib" DESTINATION "${_NJP_LIB}")
        endif()
        if(EXISTS "${_CUDA}/lib/x64/cudart.lib")
            file(COPY "${_CUDA}/lib/x64/cudart.lib" DESTINATION "${_NJP_LIB}")
        endif()

        # 复制 DLL
        if(EXISTS "${_CUDA}/bin/nvjpeg64_12.dll")
            file(COPY "${_CUDA}/bin/nvjpeg64_12.dll" DESTINATION "${_NJP_BIN}")
        endif()
        if(EXISTS "${_CUDA}/bin/cudart64_12.dll")
            file(COPY "${_CUDA}/bin/cudart64_12.dll" DESTINATION "${_NJP_BIN}")
        endif()

        message(STATUS "[nvJPEG] SDK cached to third_party/nvjpeg/ — commit for team/CI offline use")
        _rd_create_nvjpeg_targets("${_NJP_INC}" "${_NJP_LIB}" "${_NJP_BIN}")
        return()
    endif()

    # ── Tier 3: 不可用 ──────────────────────────────────────────────────
    message(STATUS "[nvJPEG] SDK not found — GPU decode will be unavailable at runtime")
endfunction()

# ── 内部: 创建导入目标 ──────────────────────────────────────────────────
function(_rd_create_nvjpeg_targets _inc _lib _bin)
    set(NVJPEG_INCLUDE_DIR "${_inc}" PARENT_SCOPE)
    set(NVJPEG_TP_BIN     "${_bin}" PARENT_SCOPE)

    if(NOT TARGET NvJpeg::nvjpeg)
        add_library(NvJpeg::nvjpeg UNKNOWN IMPORTED GLOBAL)
        set_target_properties(NvJpeg::nvjpeg PROPERTIES
            IMPORTED_LOCATION          "${_bin}/nvjpeg64_12.dll"
            IMPORTED_IMPLIB            "${_lib}/nvjpeg.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${_inc}"
        )
    endif()

    if(NOT TARGET NvJpeg::cudart)
        add_library(NvJpeg::cudart UNKNOWN IMPORTED GLOBAL)
        set_target_properties(NvJpeg::cudart PROPERTIES
            IMPORTED_LOCATION          "${_bin}/cudart64_12.dll"
            IMPORTED_IMPLIB            "${_lib}/cudart.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${_inc}"
        )
    endif()
endfunction()
