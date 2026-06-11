# cmake/FilterAppleOpenGL.cmake
# 从 CMake 目标的链接属性中移除 AGL/OpenGL 引用
#
# 使用:
#   include(cmake/FilterAppleOpenGL.cmake)
#   rd_filter_apple_opengl(Qt6::Gui)
#   rd_filter_apple_opengl(WrapOpenGL::WrapOpenGL)

# ── 通用过滤函数 ──
# 从目标的指定属性中过滤 token 列表。
# 支持单 token（如 "AGL"）和 "-framework;AGL" 对两种格式。
function(rd_filter_link_property _target _property _tokens)
    if(NOT APPLE)
        return()
    endif()

    get_target_property(_values ${_target} ${_property})
    if(NOT _values)
        return()
    endif()

    set(_filtered "")
    list(LENGTH _values _len)
    set(_i 0)
    while(_i LESS _len)
        list(GET _values ${_i} _cur)
        set(_skip FALSE)

        # 检查单 token 格式
        foreach(_tok ${_tokens})
            if(_cur STREQUAL _tok)
                set(_skip TRUE)
                math(EXPR _i "${_i}+1")
                break()
            endif()
        endforeach()

        if(_skip)
            continue()
        endif()

        # 检查 -framework;Token 对格式
        if(_cur STREQUAL "-framework")
            math(EXPR _next "${_i}+1")
            if(_next LESS _len)
                list(GET _values ${_next} _fw)
                foreach(_tok ${_tokens})
                    if(_fw STREQUAL _tok)
                        math(EXPR _i "${_i}+2")  # 跳过整个对
                        set(_skip TRUE)
                        break()
                    endif()
                endforeach()
            endif()
        endif()

        if(NOT _skip)
            list(APPEND _filtered "${_cur}")
            math(EXPR _i "${_i}+1")
        endif()
    endwhile()

    if(NOT _filtered STREQUAL _values)
        set_property(TARGET ${_target} PROPERTY ${_property} "${_filtered}")
        message(STATUS "[AGL Filter] Filtered ${_target} ${_property}")
    endif()
endfunction()

# ── 便捷包装 ──
# 从目标中清理所有已知的 AGL/OpenGL 传播路径
function(rd_filter_apple_opengl _target)
    if(NOT APPLE)
        return()
    endif()

    set(_agl_tokens "AGL" "OpenGL")

    # INTERFACE_LINK_LIBRARIES
    rd_filter_link_property(${_target} INTERFACE_LINK_LIBRARIES "${_agl_tokens}")

    # IMPORTED_LINK_INTERFACE_LIBRARIES
    rd_filter_link_property(${_target} IMPORTED_LINK_INTERFACE_LIBRARIES "${_agl_tokens}")

    # INTERFACE_LINK_OPTIONS
    rd_filter_link_property(${_target} INTERFACE_LINK_OPTIONS "${_agl_tokens}")

    # 配置特定的属性
    foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        rd_filter_link_property(${_target} IMPORTED_LINK_INTERFACE_LIBRARIES_${_cfg} "${_agl_tokens}")
        rd_filter_link_property(${_target} IMPORTED_LINK_OPTIONS_${_cfg} "${_agl_tokens}")
    endforeach()
endfunction()
