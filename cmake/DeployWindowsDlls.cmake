# cmake/DeployWindowsDlls.cmake
# Windows 运行时 DLL 部署: Qt 运行时 + 插件 + OpenSSL
#
# 使用:
#   include(cmake/DeployWindowsDlls.cmake)
#   rd_deploy_windows_runtime(CrossRemoteDesktop)
#
# 依赖: QT6_ROOT_DIR (SetupQt6.cmake), OPENSSL_TP_BIN (SetupOpenSSL.cmake), TURBOJPEG_TP_BIN (SetupLibJpegTurbo.cmake)

function(rd_deploy_windows_runtime _target)
    if(NOT WIN32)
        return()
    endif()

    set(_qt_bin "${QT6_ROOT_DIR}/bin")
    set(_qt_plugins "${QT6_ROOT_DIR}/plugins")
    set(_out_dir "$<TARGET_FILE_DIR:${_target}>")

    # ── Qt 运行时 DLL ──
    set(_qt_modules Core Gui Widgets Network OpenGL OpenGLWidgets Concurrent Svg)
    foreach(_mod ${_qt_modules})
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_qt_bin}/$<IF:$<CONFIG:Debug>,Qt6${_mod}d.dll,Qt6${_mod}.dll>"
                "${_out_dir}/"
            COMMENT "Copying Qt6${_mod} DLL"
        )
    endforeach()

    # ── 平台插件 ──
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt_plugins}/platforms/$<IF:$<CONFIG:Debug>,qwindowsd.dll,qwindows.dll>"
            "${_out_dir}/platforms/"
        COMMENT "Copying Qt platform plugin"
    )

    # ── 样式插件 ──
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}/styles"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt_plugins}/styles/$<IF:$<CONFIG:Debug>,qmodernwindowsstyled.dll,qmodernwindowsstyle.dll>"
            "${_out_dir}/styles/"
        COMMENT "Copying Qt style plugin"
    )

    # ── 图像格式插件（JPEG + SVG）──
    if(EXISTS "${_qt_plugins}/imageformats")
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}/imageformats"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_qt_plugins}/imageformats/$<IF:$<CONFIG:Debug>,qjpegd.dll,qjpeg.dll>"
                "${_out_dir}/imageformats/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_qt_plugins}/imageformats/$<IF:$<CONFIG:Debug>,qsvgd.dll,qsvg.dll>"
                "${_out_dir}/imageformats/"
            COMMENT "Copying Qt imageformat plugins (JPEG + SVG)"
        )
    endif()

    # ── SVG 图标引擎插件 ──
    if(EXISTS "${_qt_plugins}/iconengines")
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}/iconengines"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_qt_plugins}/iconengines/$<IF:$<CONFIG:Debug>,qsvgicond.dll,qsvgicon.dll>"
                "${_out_dir}/iconengines/"
            COMMENT "Copying Qt SVG icon engine plugin"
        )
    endif()

    # ── OpenSSL DLL ──
    if(EXISTS "${OPENSSL_TP_BIN}/libssl-3-x64.dll")
        set(_ssl_src "$<IF:$<CONFIG:Debug>,libssl-3-x64D.dll,libssl-3-x64.dll>")
        set(_crypto_src "$<IF:$<CONFIG:Debug>,libcrypto-3-x64D.dll,libcrypto-3-x64.dll>")
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OPENSSL_TP_BIN}/${_ssl_src}"
                "${_out_dir}/libssl-3-x64.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OPENSSL_TP_BIN}/${_crypto_src}"
                "${_out_dir}/libcrypto-3-x64.dll"
            COMMENT "Copying OpenSSL DLLs"
        )
    endif()

    # ── TurboJPEG DLL ──
    if(DEFINED TURBOJPEG_TP_BIN AND EXISTS "${TURBOJPEG_TP_BIN}/turbojpeg.dll")
        set(_tj_src "$<IF:$<CONFIG:Debug>,turbojpegD.dll,turbojpeg.dll>")
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${TURBOJPEG_TP_BIN}/${_tj_src}"
                "${_out_dir}/turbojpeg.dll"
            COMMENT "Copying TurboJPEG DLL"
        )
    endif()

    # ── nvJPEG GPU 解码 DLL（可选，运行时 QLibrary 加载）──
    if(DEFINED NVJPEG_TP_BIN AND EXISTS "${NVJPEG_TP_BIN}")
        file(GLOB _nvjpeg_dlls "${NVJPEG_TP_BIN}/*.dll")
        foreach(_dll ${_nvjpeg_dlls})
            get_filename_component(_dll_name "${_dll}" NAME)
            add_custom_command(TARGET ${_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dll}"
                    "${_out_dir}/"
                COMMENT "Copying nvJPEG: ${_dll_name}"
            )
        endforeach()
    endif()

    # ── Qt TLS 插件 ──
    if(EXISTS "${_qt_plugins}/tls")
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}/tls"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_qt_plugins}/tls"
                "${_out_dir}/tls"
            COMMENT "Copying Qt TLS plugins"
        )
    endif()
endfunction()
