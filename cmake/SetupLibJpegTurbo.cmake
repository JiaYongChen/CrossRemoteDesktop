# SetupLibJpegTurbo.cmake
# ──────────────────────────────────────────────────────────────────────────
# 三级机制:
#  1. 检测 third_party/libjpeg-turbo/ 预编译库 → 直接用（离线）
#  2. 缺失时调用 vcpkg 下载预编译包 → 缓存到 third_party/libjpeg-turbo/
#  3. vcpkg 不可用 → FATAL_ERROR 提示用户
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
    set(_TRIPLET        "x64-windows")
    set(_TJ_BIN         "${_TJ_TP}/bin/${PLATFORM_NAME}-${PLATFORM_ARCH}")
else()
    set(_TJ_FILE "libturbojpeg.a")
    if(APPLE)
        if(PLATFORM_ARCH STREQUAL "ARM64")
            set(_TRIPLET "arm64-osx")
        else()
            set(_TRIPLET "x64-osx")
        endif()
    else()
        if(PLATFORM_ARCH STREQUAL "ARM64")
            set(_TRIPLET "arm64-linux")
        else()
            set(_TRIPLET "x64-linux")
        endif()
    endif()
endif()

# ══════════════════════════════════════════════════════════════════════════
# 1) 检查预编译库是否完整
# ══════════════════════════════════════════════════════════════════════════
set(_READY TRUE)
if(NOT EXISTS "${_TJ_INC}/turbojpeg.h")
    set(_READY FALSE)
endif()
if(WIN32)
    if(NOT EXISTS "${_TJ_LIB}/${_TJ_IMPLIB}")
        set(_READY FALSE)
    endif()
    if(NOT EXISTS "${_TJ_BIN}/${_TJ_DLL_REL}" OR NOT EXISTS "${_TJ_BIN}/${_TJ_DLL_DBG}")
        set(_READY FALSE)
    endif()
else()
    if(NOT EXISTS "${_TJ_LIB}/${_TJ_FILE}")
        set(_READY FALSE)
    endif()
endif()

# ══════════════════════════════════════════════════════════════════════════
# 2) 缺失 → vcpkg 自动下载预编译包
# ══════════════════════════════════════════════════════════════════════════
if(NOT _READY)
    if(NOT DEFINED ENV{VCPKG_ROOT})
        message(FATAL_ERROR
            "[libjpeg-turbo] third_party/libjpeg-turbo/ 预编译库不完整，且 VCPKG_ROOT 未设置。\n"
            "  方案1: 安装 vcpkg\n"
            "    git clone https://github.com/microsoft/vcpkg.git C:/vcpkg\n"
            "    cd C:/vcpkg && bootstrap-vcpkg.bat\n"
            "    setx VCPKG_ROOT C:/vcpkg\n"
            "  方案2: 手动放置 libjpeg-turbo 预编译库到 ${_TJ_LIB}/")
    endif()

    message(STATUS "[libjpeg-turbo] 预编译库缺失，vcpkg 自动下载（${_TRIPLET}）...")

    set(_VCPKG "$ENV{VCPKG_ROOT}/vcpkg.exe")
    if(NOT EXISTS "${_VCPKG}")
        message(FATAL_ERROR
            "[libjpeg-turbo] vcpkg.exe 未找到: ${_VCPKG}\n"
            "  请确认 VCPKG_ROOT 环境变量指向正确的 vcpkg 安装目录。")
    endif()

    set(_INSTALL_DIR "${CMAKE_BINARY_DIR}/_vcpkg_turbojpeg")

    execute_process(
        COMMAND "${_VCPKG}" install "libjpeg-turbo:${_TRIPLET}"
                "--x-install-root=${_INSTALL_DIR}"
        RESULT_VARIABLE _rc
        TIMEOUT 600
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "[libjpeg-turbo] vcpkg install libjpeg-turbo:${_TRIPLET} 失败 (exit=${_rc})。\n"
            "  请检查网络连接或手动放置预编译库到 ${_TJ_LIB}/")
    endif()

    # ── 复制产物到 third_party/libjpeg-turbo/ ──
    set(_SRC "${_INSTALL_DIR}/${_TRIPLET}")

    # 头文件
    file(MAKE_DIRECTORY "${_TJ_INC}")
    file(COPY "${_SRC}/include/" DESTINATION "${_TJ_INC}")

    if(WIN32)
        # 导入库（Release 和 Debug 各一份，内容相同但分开存放供 CMake 区分）
        file(MAKE_DIRECTORY "${_TJ_LIB}")
        file(COPY "${_SRC}/lib/turbojpeg.lib" DESTINATION "${_TJ_LIB}")

        # DLL: vcpkg 的 Release/Debug DLL 同名，按项目惯例用 D 后缀区分
        file(MAKE_DIRECTORY "${_TJ_BIN}")
        # 先复制 Debug → 重命名为 turbojpegD.dll
        if(EXISTS "${_SRC}/debug/bin/turbojpeg.dll")
            file(COPY "${_SRC}/debug/bin/turbojpeg.dll" DESTINATION "${_TJ_BIN}")
            file(RENAME "${_TJ_BIN}/turbojpeg.dll" "${_TJ_BIN}/${_TJ_DLL_DBG}")
        endif()
        # 再复制 Release → 保持 turbojpeg.dll（不会覆盖已重命名的 Debug）
        if(EXISTS "${_SRC}/bin/turbojpeg.dll")
            file(COPY "${_SRC}/bin/turbojpeg.dll" DESTINATION "${_TJ_BIN}")
        endif()
    else()
        file(MAKE_DIRECTORY "${_TJ_LIB}")
        file(COPY "${_SRC}/lib/libturbojpeg.a" DESTINATION "${_TJ_LIB}")
        file(RENAME "${_TJ_LIB}/libturbojpeg.a" "${_TJ_LIB}/${_TJ_FILE}")
    endif()

    # 清理临时目录
    file(REMOVE_RECURSE "${_INSTALL_DIR}")

    message(STATUS "[libjpeg-turbo] 已缓存到 ${_TJ_TP}（请提交 git 供离线使用）")
endif()

# ══════════════════════════════════════════════════════════════════════════
# 3) 创建 imported target
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
