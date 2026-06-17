# cmake/SetupCuda.cmake
# CUDA Runtime + nvJPEG — 统一从 third_party/cuda/ 缓存
# 编译时直接链接，运行时 DLL 必须存在
#   输出: Cuda::cudart, Cuda::nvjpeg (imported targets)
#         CUDA_AVAILABLE, NVJPEG_AVAILABLE, CUDA_TP_BIN
#
include_guard(GLOBAL)

function(rd_setup_cuda)
    if(NOT WIN32)
        return()
    endif()

    set(_TP  "${CMAKE_SOURCE_DIR}/third_party/cuda")
    set(_LIB "${_TP}/lib/Windows-x64")
    set(_BIN "${_TP}/bin/Windows-x64")
    set(_INC "${_TP}/include")

    # ── CUDA Runtime ──
    if(EXISTS "${_LIB}/cudart.lib" AND EXISTS "${_INC}/cuda_runtime_api.h")
        message(STATUS "[CUDA] Using cached SDK from third_party/cuda/")
        if(NOT TARGET Cuda::cudart)
            add_library(Cuda::cudart UNKNOWN IMPORTED GLOBAL)
            set_target_properties(Cuda::cudart PROPERTIES
                IMPORTED_IMPLIB                "${_LIB}/cudart.lib"
                INTERFACE_INCLUDE_DIRECTORIES  "${_INC}"
            )
        endif()
        set(CUDA_AVAILABLE TRUE PARENT_SCOPE)
    else()
        message(STATUS "[CUDA] Not found — GPU features unavailable")
        set(CUDA_AVAILABLE FALSE PARENT_SCOPE)
    endif()

    # ── nvJPEG ──
    if(EXISTS "${_LIB}/nvjpeg.lib")
        if(NOT TARGET Cuda::nvjpeg)
            add_library(Cuda::nvjpeg UNKNOWN IMPORTED GLOBAL)
            set_target_properties(Cuda::nvjpeg PROPERTIES
                IMPORTED_IMPLIB                "${_LIB}/nvjpeg.lib"
                INTERFACE_INCLUDE_DIRECTORIES  "${_INC}"
            )
        endif()
        set(NVJPEG_AVAILABLE TRUE PARENT_SCOPE)
        set(CUDA_TP_BIN "${_BIN}" PARENT_SCOPE)
    else()
        set(NVJPEG_AVAILABLE FALSE PARENT_SCOPE)
    endif()
endfunction()
