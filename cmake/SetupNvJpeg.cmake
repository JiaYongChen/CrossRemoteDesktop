# cmake/SetupNvJpeg.cmake
# nvJPEG — 编译时链接 nvjpeg.lib，运行时 Delay-Load nvjpeg64_12.dll
# CUDA 函数通过 QLibrary 运行时加载（cudart64_12.dll 由 NVIDIA 驱动提供）

include_guard(GLOBAL)

function(rd_setup_nvjpeg)
    if(NOT WIN32)
        return()
    endif()

    set(_LIB "${CMAKE_SOURCE_DIR}/third_party/nvjpeg/lib/Windows-x64")
    set(_BIN "${CMAKE_SOURCE_DIR}/third_party/nvjpeg/bin/Windows-x64")

    if(EXISTS "${_LIB}/nvjpeg.lib")
        message(STATUS "[nvJPEG] Using cached import library from third_party/nvjpeg/")
        if(NOT TARGET NvJpeg::nvjpeg)
            add_library(NvJpeg::nvjpeg UNKNOWN IMPORTED GLOBAL)
            set_target_properties(NvJpeg::nvjpeg PROPERTIES
                IMPORTED_LOCATION_DEBUG          "${_LIB}/nvjpeg.lib"
                IMPORTED_LOCATION_RELEASE        "${_LIB}/nvjpeg.lib"
                IMPORTED_LOCATION_RELWITHDEBINFO "${_LIB}/nvjpeg.lib"
                IMPORTED_LOCATION_MINSIZEREL     "${_LIB}/nvjpeg.lib"
            )
        endif()
        set(NVJPEG_TP_BIN "${_BIN}" PARENT_SCOPE)
    else()
        message(STATUS "[nvJPEG] Not found — GPU decode unavailable")
    endif()
endfunction()
