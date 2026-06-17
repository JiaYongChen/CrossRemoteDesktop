# cmake/SetupNvJpeg.cmake
# ──────────────────────────────────────────────────────────────────────────
# nvJPEG GPU JPEG 库（编译时链接）
#
# 直接从 third_party/nvjpeg/ 预编译缓存读取（已提交 git）
# 缓存缺失 → 跳过（应用运行时自动降级到 CPU 解码）
#
# 开发者：通过 CUDA Toolkit 获取 nvJPEG SDK → 按约定缓存到 third_party/ → 提交 git
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

    # ══════════════════════════════════════════════════════════════════════════
    # 1) 检测 third_party 缓存
    # ══════════════════════════════════════════════════════════════════════════
    if(EXISTS "${_NJP_LIB}/nvjpeg.lib" AND EXISTS "${_NJP_LIB}/cudart.lib"
       AND EXISTS "${_NJP_INC}/nvjpeg.h" AND EXISTS "${_NJP_INC}/cuda_runtime_api.h"
       AND EXISTS "${_NJP_INC}/crt/host_defines.h")
        message(STATUS "[nvJPEG] Using cached SDK from third_party/nvjpeg/ (CUDA 12.x)")
        _rd_create_nvjpeg_targets("${_NJP_INC}" "${_NJP_LIB}" "${_NJP_BIN}")
        return()
    endif()

    # ══════════════════════════════════════════════════════════════════════════
    # 2) 缓存缺失 → 跳过
    # ══════════════════════════════════════════════════════════════════════════
    message(STATUS "[nvJPEG] SDK not found in third_party/ — GPU decode will be unavailable at runtime")
    message(STATUS "[nvJPEG]   To enable: install CUDA Toolkit → copy nvJPEG SDK to third_party/nvjpeg/ → commit git")
endfunction()

# ── 内部: 创建导入目标 ──────────────────────────────────────────────────
function(_rd_create_nvjpeg_targets _inc _lib _bin)
    set(NVJPEG_INCLUDE_DIR "${_inc}" PARENT_SCOPE)
    set(NVJPEG_TP_BIN     "${_bin}" PARENT_SCOPE)

    if(NOT TARGET NvJpeg::nvjpeg)
        add_library(NvJpeg::nvjpeg SHARED IMPORTED GLOBAL)
        set_target_properties(NvJpeg::nvjpeg PROPERTIES
            IMPORTED_LOCATION          "${_bin}/nvjpeg64_12.dll"
            IMPORTED_IMPLIB            "${_lib}/nvjpeg.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${_inc}"
        )
    endif()

    if(NOT TARGET NvJpeg::cudart)
        add_library(NvJpeg::cudart SHARED IMPORTED GLOBAL)
        set_target_properties(NvJpeg::cudart PROPERTIES
            IMPORTED_LOCATION          "${_bin}/cudart64_12.dll"
            IMPORTED_IMPLIB            "${_lib}/cudart.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${_inc}"
        )
    endif()
endfunction()
