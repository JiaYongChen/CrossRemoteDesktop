# SetupOpenSSL.cmake
# ──────────────────────────────────────────────────────────────────────────
# 三级机制:
#  1. 检测 third_party/openssl/ 预编译库 → 直接用（离线，秒级）
#  2. 缺失时调用 vcpkg 下载预编译包 → 缓存到 third_party/openssl/
#  3. vcpkg 不可用 → FATAL_ERROR 提示用户
#
# 输出: OpenSSL::SSL, OpenSSL::Crypto (imported, per-config)
#        OPENSSL_INCLUDE_DIR, OPENSSL_TP_BIN
# ──────────────────────────────────────────────────────────────────────────

set(_SSL_TP   "${CMAKE_SOURCE_DIR}/third_party/openssl")
set(_SSL_INC  "${_SSL_TP}/include")
set(_SSL_LIB  "${_SSL_TP}/lib/${PLATFORM_NAME}-${PLATFORM_ARCH}")
set(_SSL_BIN  "${_SSL_TP}/bin/${PLATFORM_NAME}-${PLATFORM_ARCH}")

# ── 平台特定文件名 ──────────────────────────────────────────────────────
if(WIN32)
    set(_SSL_REL_LIB     "libssl.lib")
    set(_SSL_DBG_LIB     "libsslD.lib")
    set(_CRYPTO_REL_LIB  "libcrypto.lib")
    set(_CRYPTO_DBG_LIB  "libcryptoD.lib")
    set(_SSL_REL_DLL     "libssl-3-x64.dll")
    set(_SSL_DBG_DLL     "libssl-3-x64D.dll")
    set(_CRYPTO_REL_DLL  "libcrypto-3-x64.dll")
    set(_CRYPTO_DBG_DLL  "libcrypto-3-x64D.dll")
    set(_TRIPLET         "x64-windows")
else()
    set(_SSL_REL_LIB     "libssl.a")
    set(_SSL_DBG_LIB     "libsslD.a")
    set(_CRYPTO_REL_LIB  "libcrypto.a")
    set(_CRYPTO_DBG_LIB  "libcryptoD.a")
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
if(NOT EXISTS "${_SSL_INC}/openssl/ssl.h")
    set(_READY FALSE)
endif()
foreach(_f "${_SSL_REL_LIB}" "${_SSL_DBG_LIB}" "${_CRYPTO_REL_LIB}" "${_CRYPTO_DBG_LIB}")
    if(NOT EXISTS "${_SSL_LIB}/${_f}")
        set(_READY FALSE)
    endif()
endforeach()
if(WIN32)
    foreach(_f "${_SSL_REL_DLL}" "${_SSL_DBG_DLL}" "${_CRYPTO_REL_DLL}" "${_CRYPTO_DBG_DLL}")
        if(NOT EXISTS "${_SSL_BIN}/${_f}")
            set(_READY FALSE)
        endif()
    endforeach()
endif()

# ══════════════════════════════════════════════════════════════════════════
# 2) 缺失 → vcpkg 自动下载预编译包
# ══════════════════════════════════════════════════════════════════════════
if(NOT _READY)
    if(NOT DEFINED ENV{VCPKG_ROOT})
        message(FATAL_ERROR
            "[OpenSSL] third_party/openssl/ 预编译库不完整，且 VCPKG_ROOT 未设置。\n"
            "  方案1: 安装 vcpkg\n"
            "    git clone https://github.com/microsoft/vcpkg.git C:/vcpkg\n"
            "    cd C:/vcpkg && bootstrap-vcpkg.bat\n"
            "    setx VCPKG_ROOT C:/vcpkg\n"
            "  方案2: 手动放置 OpenSSL 预编译库到 ${_SSL_LIB}/")
    endif()

    message(STATUS "[OpenSSL] 预编译库缺失，vcpkg 自动下载（${_TRIPLET}）...")

    set(_VCPKG "$ENV{VCPKG_ROOT}/vcpkg.exe")
    if(NOT EXISTS "${_VCPKG}")
        message(FATAL_ERROR
            "[OpenSSL] vcpkg.exe 未找到: ${_VCPKG}\n"
            "  请确认 VCPKG_ROOT 环境变量指向正确的 vcpkg 安装目录。")
    endif()

    set(_INSTALL_DIR "${CMAKE_BINARY_DIR}/_vcpkg_openssl")

    execute_process(
        COMMAND "${_VCPKG}" install "openssl:${_TRIPLET}"
                "--x-install-root=${_INSTALL_DIR}"
        RESULT_VARIABLE _rc
        TIMEOUT 600
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "[OpenSSL] vcpkg install openssl:${_TRIPLET} 失败 (exit=${_rc})。\n"
            "  请检查网络连接或手动放置预编译库到 ${_SSL_LIB}/")
    endif()

    # ── 复制产物到 third_party/openssl/ ──
    set(_SRC "${_INSTALL_DIR}/${_TRIPLET}")

    # 头文件
    file(MAKE_DIRECTORY "${_SSL_INC}/openssl")
    file(COPY "${_SRC}/include/openssl/" DESTINATION "${_SSL_INC}/openssl")

    # 静态库 / 导入库
    file(MAKE_DIRECTORY "${_SSL_LIB}")
    # vcpkg 的 openssl port 只生成一个导入库 openssl.lib（同时包含 ssl + crypto）
    # 我们需要复制两份命名为 libssl.lib 和 libcrypto.lib
    if(WIN32)
        # Release
        file(COPY "${_SRC}/lib/openssl.lib" DESTINATION "${_SSL_LIB}")
        file(RENAME "${_SSL_LIB}/openssl.lib" "${_SSL_LIB}/${_SSL_REL_LIB}")
        file(COPY "${_SSL_LIB}/${_SSL_REL_LIB}" "${_SSL_LIB}/${_CRYPTO_REL_LIB}")
        # Debug
        file(COPY "${_SRC}/debug/lib/openssl.lib" DESTINATION "${_SSL_LIB}")
        file(RENAME "${_SSL_LIB}/openssl.lib" "${_SSL_LIB}/${_SSL_DBG_LIB}")
        file(COPY "${_SSL_LIB}/${_SSL_DBG_LIB}" "${_SSL_LIB}/${_CRYPTO_DBG_LIB}")

        # DLL
        file(MAKE_DIRECTORY "${_SSL_BIN}")
        # 用 glob 获取实际 DLL 名称（版本号可能变化）
        file(GLOB _ssl_dll  "${_SRC}/bin/libssl-*.dll")
        file(GLOB _crypto_dll "${_SRC}/bin/libcrypto-*.dll")
        file(GLOB _ssl_dbg_dll  "${_SRC}/debug/bin/libssl-*.dll")
        file(GLOB _crypto_dbg_dll "${_SRC}/debug/bin/libcrypto-*.dll")

        if(_ssl_dll AND _crypto_dll)
            file(COPY ${_ssl_dll} DESTINATION "${_SSL_BIN}")
            file(COPY ${_crypto_dll} DESTINATION "${_SSL_BIN}")
        endif()
        if(_ssl_dbg_dll AND _crypto_dbg_dll)
            # 重命名为 D 后缀
            get_filename_component(_sn "${_ssl_dbg_dll}" NAME)
            get_filename_component(_cn "${_crypto_dbg_dll}" NAME)
            file(COPY ${_ssl_dbg_dll} DESTINATION "${_SSL_BIN}")
            file(COPY ${_crypto_dbg_dll} DESTINATION "${_SSL_BIN}")
            # 重命名: libssl-3-x64.dll → libssl-3-x64D.dll
            string(REGEX REPLACE "\\.dll$" "D.dll" _sn_d "${_sn}")
            string(REGEX REPLACE "\\.dll$" "D.dll" _cn_d "${_cn}")
            if(EXISTS "${_SSL_BIN}/${_sn}" AND NOT _sn STREQUAL _sn_d)
                file(RENAME "${_SSL_BIN}/${_sn}" "${_SSL_BIN}/${_sn_d}")
            endif()
            if(EXISTS "${_SSL_BIN}/${_cn}" AND NOT _cn STREQUAL _cn_d)
                file(RENAME "${_SSL_BIN}/${_cn}" "${_SSL_BIN}/${_cn_d}")
            endif()
        endif()
    else()
        # Unix: vcpkg 默认编译静态库
        file(COPY "${_SRC}/lib/libssl.a" DESTINATION "${_SSL_LIB}")
        file(RENAME "${_SSL_LIB}/libssl.a" "${_SSL_LIB}/${_SSL_REL_LIB}")
        file(COPY "${_SSL_LIB}/${_SSL_REL_LIB}" "${_SSL_LIB}/${_CRYPTO_REL_LIB}")
        file(COPY "${_SRC}/debug/lib/libssl.a" DESTINATION "${_SSL_LIB}")
        file(RENAME "${_SSL_LIB}/libssl.a" "${_SSL_LIB}/${_SSL_DBG_LIB}")
        file(COPY "${_SSL_LIB}/${_SSL_DBG_LIB}" "${_SSL_LIB}/${_CRYPTO_DBG_LIB}")
    endif()

    # 清理临时目录
    file(REMOVE_RECURSE "${_INSTALL_DIR}")

    message(STATUS "[OpenSSL] 已缓存到 ${_SSL_TP}（请提交 git 供离线使用）")
endif()

# ══════════════════════════════════════════════════════════════════════════
# 3) 创建 imported targets
# ══════════════════════════════════════════════════════════════════════════
message(STATUS "[OpenSSL] Using ${_SSL_TP}")

set(OPENSSL_INCLUDE_DIR "${_SSL_INC}")
set(OPENSSL_TP_BIN      "${_SSL_BIN}")

if(NOT TARGET OpenSSL::SSL)
    add_library(OpenSSL::SSL UNKNOWN IMPORTED GLOBAL)
    set_target_properties(OpenSSL::SSL PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES    "${_SSL_INC}"
        IMPORTED_LOCATION_DEBUG          "${_SSL_LIB}/${_SSL_DBG_LIB}"
        IMPORTED_LOCATION_RELEASE        "${_SSL_LIB}/${_SSL_REL_LIB}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${_SSL_LIB}/${_SSL_REL_LIB}"
        IMPORTED_LOCATION_MINSIZEREL     "${_SSL_LIB}/${_SSL_REL_LIB}"
    )
endif()
if(NOT TARGET OpenSSL::Crypto)
    add_library(OpenSSL::Crypto UNKNOWN IMPORTED GLOBAL)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES    "${_SSL_INC}"
        IMPORTED_LOCATION_DEBUG          "${_SSL_LIB}/${_CRYPTO_DBG_LIB}"
        IMPORTED_LOCATION_RELEASE        "${_SSL_LIB}/${_CRYPTO_REL_LIB}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${_SSL_LIB}/${_CRYPTO_REL_LIB}"
        IMPORTED_LOCATION_MINSIZEREL     "${_SSL_LIB}/${_CRYPTO_REL_LIB}"
    )
endif()
