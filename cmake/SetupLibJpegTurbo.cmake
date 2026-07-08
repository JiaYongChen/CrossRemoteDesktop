# SetupLibJpegTurbo.cmake
# ──────────────────────────────────────────────────────────────────────────
# 直接从 third_party/libjpeg-turbo/ 预编译缓存读取（已提交 git）
# 缓存缺失时 FATAL_ERROR —— 开发者应通过 vcpkg 获取后缓存到 third_party/
#
# 注意：项目仅使用 TurboJPEG API（turbojpeg.dll），不使用经典 libjpeg API（jpeg62.dll）。
#       vcpkg 安装 libjpeg-turbo 时两者均生成，只需缓存 TurboJPEG 相关文件。
#
# Windows: x64-windows triplet → turbojpeg.dll + turbojpeg.lib（导入库）
# Unix:    静态库 libturbojpeg.a
#
# 输出: LibJpegTurbo::turbojpeg (imported)
#        TURBOJPEG_INCLUDE_DIR, TURBOJPEG_TP_BIN
# ──────────────────────────────────────────────────────────────────────────

set(_TJ_TP  "${CMAKE_SOURCE_DIR}/third_party/libjpeg-turbo")
set(_TJ_INC "${_TJ_TP}/include")
set(_TJ_LIB "${_TJ_TP}/lib/${PLATFORM_NAME}-${PLATFORM_ARCH}")

# ── 平台特定文件名 ──────────────────────────────────────────────────────
if(WIN32)
    set(_TJ_IMPLIB      "turbojpeg.lib")       # 导入库（Debug/Release 相同）
    set(_TJ_DLL_REL     "turbojpeg.dll")
    set(_TJ_DLL_DBG     "turbojpegD.dll")
    set(_TJ_BIN         "${_TJ_TP}/bin/${PLATFORM_NAME}-${PLATFORM_ARCH}")
else()
    set(_TJ_FILE "libturbojpeg.a")
endif()

# ══════════════════════════════════════════════════════════════════════════
# 1) 检查预编译缓存是否完整
# ══════════════════════════════════════════════════════════════════════════
set(_MISSING "")
if(NOT EXISTS "${_TJ_INC}/turbojpeg.h")
    list(APPEND _MISSING "  - 头文件: ${_TJ_INC}/turbojpeg.h")
endif()
if(WIN32)
    if(NOT EXISTS "${_TJ_LIB}/${_TJ_IMPLIB}")
        list(APPEND _MISSING "  - 导入库: ${_TJ_LIB}/${_TJ_IMPLIB}")
    endif()
    if(NOT EXISTS "${_TJ_BIN}/${_TJ_DLL_REL}")
        list(APPEND _MISSING "  - DLL(Release): ${_TJ_BIN}/${_TJ_DLL_REL}")
    endif()
    if(NOT EXISTS "${_TJ_BIN}/${_TJ_DLL_DBG}")
        list(APPEND _MISSING "  - DLL(Debug): ${_TJ_BIN}/${_TJ_DLL_DBG}")
    endif()
else()
    if(NOT EXISTS "${_TJ_LIB}/${_TJ_FILE}")
        list(APPEND _MISSING "  - 库文件: ${_TJ_LIB}/${_TJ_FILE}")
    endif()
endif()

if(_MISSING)
    string(JOIN "\n" _missing_list ${_MISSING})
    message(FATAL_ERROR
        "[libjpeg-turbo] third_party/libjpeg-turbo/ 预编译缓存不完整，缺少以下文件:\n"
        "${_missing_list}\n\n"
        "  请通过 vcpkg 下载预编译包并缓存到 third_party/libjpeg-turbo/:\n"
        "    vcpkg install libjpeg-turbo:x64-windows\n"
        "  然后将产物按以下结构重组并提交 git:\n"
        "    third_party/libjpeg-turbo/\n"
        "      include/             ← 头文件\n"
        "      lib/Windows-x64/      ← 导入库 (turbojpeg.lib)\n"
        "      bin/Windows-x64/      ← DLL (turbojpeg.dll, turbojpegD.dll)")
endif()

# ══════════════════════════════════════════════════════════════════════════
# 2) 创建 imported target
# ══════════════════════════════════════════════════════════════════════════
message(STATUS "[libjpeg-turbo] Using ${_TJ_TP}")

set(TURBOJPEG_INCLUDE_DIR "${_TJ_INC}")
set(TURBOJPEG_TP_BIN      "${_TJ_BIN}")

if(NOT TARGET LibJpegTurbo::turbojpeg)
    add_library(LibJpegTurbo::turbojpeg UNKNOWN IMPORTED GLOBAL)
    if(WIN32)
        # x64-windows triplet → 动态库（turbojpeg.dll + turbojpeg.lib 导入库）
        set_target_properties(LibJpegTurbo::turbojpeg PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES    "${_TJ_INC}"
            IMPORTED_LOCATION_DEBUG          "${_TJ_LIB}/${_TJ_IMPLIB}"
            IMPORTED_LOCATION_RELEASE        "${_TJ_LIB}/${_TJ_IMPLIB}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${_TJ_LIB}/${_TJ_IMPLIB}"
            IMPORTED_LOCATION_MINSIZEREL     "${_TJ_LIB}/${_TJ_IMPLIB}"
        )
    else()
        # Unix → 静态库
        add_library(LibJpegTurbo::turbojpeg STATIC IMPORTED GLOBAL)
        set_target_properties(LibJpegTurbo::turbojpeg PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES    "${_TJ_INC}"
            IMPORTED_LOCATION_DEBUG          "${_TJ_LIB}/${_TJ_FILE}"
            IMPORTED_LOCATION_RELEASE        "${_TJ_LIB}/${_TJ_FILE}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${_TJ_LIB}/${_TJ_FILE}"
            IMPORTED_LOCATION_MINSIZEREL     "${_TJ_LIB}/${_TJ_FILE}"
        )
    endif()
endif()
