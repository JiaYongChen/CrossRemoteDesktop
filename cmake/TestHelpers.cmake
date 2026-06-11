# cmake/TestHelpers.cmake
# 测试目标创建辅助函数
#
# 使用:
#   include(../cmake/TestHelpers.cmake)
#   add_rd_test(
#       TARGET         test_threadmanager
#       NAME           ThreadManagerTest
#       SOURCES        test_threadmanager.cpp
#       EXTRA_SOURCES  ../src/xxx.cpp          # 可选
#       LIBRARIES      Qt6::Core Qt6::Test ...
#       TIMEOUT        30
#       LABELS         unit threading manager
#       NO_OPENGL                              # 可选标志
#       EXTRA_ENV      "KEY=VALUE" ...         # 可选额外环境变量
#   )

function(add_rd_test)
    set(_options NO_OPENGL)
    set(_oneValueArgs TARGET NAME TIMEOUT)
    set(_multiValueArgs SOURCES EXTRA_SOURCES LIBRARIES LABELS EXTRA_ENV)
    cmake_parse_arguments(ARG "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

    # ── 1. 创建可执行目标 ──
    qt_add_executable(${ARG_TARGET}
        ${ARG_SOURCES}
        ${ARG_EXTRA_SOURCES}
    )

    # ── 2. 链接库 ──
    target_link_libraries(${ARG_TARGET} PRIVATE ${ARG_LIBRARIES})

    # ── 3. QT_NO_OPENGL ──
    if(ARG_NO_OPENGL)
        target_compile_definitions(${ARG_TARGET} PRIVATE QT_NO_OPENGL)
    endif()

    # ── 4. CTest 注册 ──
    add_test(
        NAME ${ARG_NAME}
        COMMAND ${ARG_TARGET}
        WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    )

    # ── 5. 测试属性 ──
    # 环境变量：_TEST_BASE_ENV（由 test/CMakeLists.txt 定义）+ EXTRA_ENV 追加
    set(_env "${_TEST_BASE_ENV}")
    if(ARG_EXTRA_ENV)
        list(APPEND _env ${ARG_EXTRA_ENV})
    endif()

    set_tests_properties(${ARG_NAME} PROPERTIES
        TIMEOUT ${ARG_TIMEOUT}
        LABELS "${ARG_LABELS}"
        ENVIRONMENT "${_env}"
    )

    # ── 6. 自动添加到聚合目标 ──
    # 所有测试 → run_all_tests
    if(TARGET run_all_tests)
        add_dependencies(run_all_tests ${ARG_TARGET})
    endif()

    # 根据 LABELS 自动归类
    foreach(_label ${ARG_LABELS})
        if(_label STREQUAL "unit" AND TARGET run_core_tests)
            add_dependencies(run_core_tests ${ARG_TARGET})
        endif()
        if(_label STREQUAL "integration" AND TARGET run_integration_tests)
            add_dependencies(run_integration_tests ${ARG_TARGET})
        endif()
        if(_label STREQUAL "performance" AND TARGET run_performance_tests)
            add_dependencies(run_performance_tests ${ARG_TARGET})
        endif()
        if(_label STREQUAL "threading" AND TARGET run_threading_tests)
            add_dependencies(run_threading_tests ${ARG_TARGET})
        endif()
    endforeach()
endfunction()
