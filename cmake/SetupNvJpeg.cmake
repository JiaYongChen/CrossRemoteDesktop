# cmake/SetupNvJpeg.cmake
# ──────────────────────────────────────────────────────────────────────────
# nvJPEG GPU JPEG 库 DLL 分发（运行时 QLibrary 加载，非编译链接）
#
# nvJPEG 随 CUDA Toolkit 分发，无独立 vcpkg 包。
# 三级机制:
#  1. 检测 third_party/nvjpeg/ 预缓存 DLL → 直接用（团队/CI 离线）
#  2. 从 CUDA Toolkit 安装目录复制 → 缓存到 third_party/nvjpeg/
#  3. 找不到 → 跳过（应用运行时自动降级到 CPU 解码）
#
# 输出: NVJPEG_TP_BIN（DLL 目录路径）
#       nvjpeg64_*.dll + cudart64_*.dll 部署到输出目录
# ──────────────────────────────────────────────────────────────────────────

include_guard(GLOBAL)

function(rd_setup_nvjpeg)
    if(NOT WIN32)
        message(STATUS "[nvJPEG] Skipped — DLL deployment only supported on Windows")
        return()
    endif()

    set(_NJP_TP  "${CMAKE_SOURCE_DIR}/third_party/nvjpeg")
    set(_NJP_BIN "${_NJP_TP}/bin/Windows-x64")

    # ── Tier 1: third_party 缓存 ─────────────────────────────────────────
    set(_NJP_DLL "${_NJP_BIN}/nvjpeg64_12.dll")
    set(_CUDA_DLL "${_NJP_BIN}/cudart64_12.dll")

    if(EXISTS "${_NJP_DLL}" AND EXISTS "${_CUDA_DLL}")
        message(STATUS "[nvJPEG] Using cached DLLs from third_party/nvjpeg/ (CUDA 12.x)")
        set(NVJPEG_TP_BIN "${_NJP_BIN}" PARENT_SCOPE)
        return()
    endif()

    # Also check CUDA 11.x cache
    set(_NJP_DLL_11 "${_NJP_BIN}/nvjpeg64_11.dll")
    set(_CUDA_DLL_11 "${_NJP_BIN}/cudart64_110.dll")
    if(EXISTS "${_NJP_DLL_11}" AND EXISTS "${_CUDA_DLL_11}")
        message(STATUS "[nvJPEG] Using cached DLLs from third_party/nvjpeg/ (CUDA 11.x)")
        set(NVJPEG_TP_BIN "${_NJP_BIN}" PARENT_SCOPE)
        return()
    endif()

    # ── Tier 2: 从 CUDA Toolkit 安装中查找 ─────────────────────────────
    set(_CUDA_BIN "")

    # 2a. 检查 CUDA_PATH 环境变量
    if(DEFINED ENV{CUDA_PATH} AND EXISTS "$ENV{CUDA_PATH}/bin/nvjpeg64_12.dll")
        set(_CUDA_BIN "$ENV{CUDA_PATH}/bin")
    endif()

    # 2b. 搜索默认安装路径（新版本优先）
    if(NOT _CUDA_BIN)
        file(GLOB _CUDA_DIRS
            "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v*")
        if(_CUDA_DIRS)
            list(SORT _CUDA_DIRS COMPARE NATURAL ORDER DESCENDING)
            foreach(_dir ${_CUDA_DIRS})
                if(EXISTS "${_dir}/bin/nvjpeg64_12.dll" OR EXISTS "${_dir}/bin/nvjpeg64_11.dll")
                    set(_CUDA_BIN "${_dir}/bin")
                    break()
                endif()
            endforeach()
        endif()
    endif()

    if(NOT _CUDA_BIN)
        message(STATUS "[nvJPEG] CUDA Toolkit not found — GPU decode unavailable")
        return()
    endif()

    message(STATUS "[nvJPEG] Found CUDA Toolkit at ${_CUDA_BIN}")

    # ── 从 CUDA Toolkit 复制 DLL 到 third_party 缓存 ──────────────────
    file(MAKE_DIRECTORY "${_NJP_BIN}")

    # 复制 nvJPEG DLL（优先 12.x，其次 11.x）
    if(EXISTS "${_CUDA_BIN}/nvjpeg64_12.dll")
        file(COPY "${_CUDA_BIN}/nvjpeg64_12.dll" DESTINATION "${_NJP_BIN}")
    endif()
    if(EXISTS "${_CUDA_BIN}/nvjpeg64_11.dll")
        file(COPY "${_CUDA_BIN}/nvjpeg64_11.dll" DESTINATION "${_NJP_BIN}")
    endif()

    # 复制 CUDA Runtime DLL（优先 12.x，其次 11.x）
    if(EXISTS "${_CUDA_BIN}/cudart64_12.dll")
        file(COPY "${_CUDA_BIN}/cudart64_12.dll" DESTINATION "${_NJP_BIN}")
    endif()
    if(EXISTS "${_CUDA_BIN}/cudart64_110.dll")
        file(COPY "${_CUDA_BIN}/cudart64_110.dll" DESTINATION "${_NJP_BIN}")
    endif()

    set(NVJPEG_TP_BIN "${_NJP_BIN}" PARENT_SCOPE)
    message(STATUS "[nvJPEG] DLLs cached to third_party/nvjpeg/ — commit for team/CI offline use")
endfunction()
