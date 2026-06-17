# SetupOpenCL.cmake
# ──────────────────────────────────────────────────────────────────────────
# OpenCL SDK — 编译时链接 OpenCL.lib，OpenCL.dll 由 GPU 驱动提供
#
# 直接从 third_party/opencl/ 预编译缓存读取（已提交 git）
#
# 输出: OpenCL::OpenCL (imported)
#       OPENCL_AVAILABLE
# ──────────────────────────────────────────────────────────────────────────

include_guard(GLOBAL)

function(rd_setup_opencl)
    set(_TP  "${CMAKE_SOURCE_DIR}/third_party/opencl")
    set(_LIB "${_TP}/lib/Windows-x64")
    set(_INC "${_TP}/include")

    if(EXISTS "${_LIB}/OpenCL.lib" AND EXISTS "${_INC}/CL/cl.h")
        message(STATUS "[OpenCL] Using cached SDK from third_party/opencl/")

        if(NOT TARGET OpenCL::OpenCL)
            add_library(OpenCL::OpenCL UNKNOWN IMPORTED GLOBAL)
            set_target_properties(OpenCL::OpenCL PROPERTIES
                IMPORTED_LOCATION_DEBUG          "${_LIB}/OpenCLd.lib"
                IMPORTED_LOCATION_RELEASE        "${_LIB}/OpenCL.lib"
                IMPORTED_LOCATION_RELWITHDEBINFO "${_LIB}/OpenCL.lib"
                IMPORTED_LOCATION_MINSIZEREL     "${_LIB}/OpenCL.lib"
                INTERFACE_INCLUDE_DIRECTORIES    "${_INC}"
            )
        endif()
        set(OPENCL_AVAILABLE TRUE PARENT_SCOPE)
    else()
        message(STATUS "[OpenCL] Not found in third_party/ — GPU decode unavailable")
        set(OPENCL_AVAILABLE FALSE PARENT_SCOPE)
    endif()
endfunction()
