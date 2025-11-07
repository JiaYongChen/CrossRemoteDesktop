cmake_minimum_required(VERSION 3.16)

# 设置项目名称
project(QtRemoteDesktopTests LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 对测试目标启用更严格的编译选项，所有警告视为错误
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# 针对Clang编译器：Qt6在C++17模式下可能使用到带message的[[nodiscard]]属性，
# 这在Clang上会作为C++20扩展发出警告。由于我们将所有警告视为错误（-Werror），
# 这里对该特定告警进行抑制，避免第三方头文件导致构建失败。
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-c++20-attribute-extensions)
endif()

# 查找Qt6组件
# 查找 Qt6 组件（仅当父项目未提前找到时才查找，避免覆盖父级Qt配置路径）
if(NOT TARGET Qt6::Core)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Core Network Test Gui Concurrent)
endif()

# 在Apple平台上，部分Qt发行版的QtGui会通过INTERFACE_LINK_LIBRARIES传递AGL/OpenGL框架，
# 这在Apple Silicon上会导致“framework 'AGL' not found”的链接错误。
# 这里对Qt6::Gui的INTERFACE_LINK_LIBRARIES进行过滤，去除AGL与OpenGL相关依赖。
# Apple 平台：不要修改 Qt6::Gui 的 INTERFACE_LINK_LIBRARIES，避免丢失必要的系统框架链接（如AppKit/CoreGraphics）
# 如需避免OpenGL相关路径，请在具体测试目标上使用：target_compile_definitions(<target> PRIVATE QT_NO_OPENGL)

# 进一步过滤 Qt6::Gui 的 INTERFACE_LINK_OPTIONS 中的 "-framework AGL/OpenGL"
get_target_property(_qtgui_link_opts Qt6::Gui INTERFACE_LINK_OPTIONS)
if(APPLE AND _qtgui_link_opts)
    set(_filtered_qtgui_link_opts "")
    list(LENGTH _qtgui_link_opts _len)
    set(_i 0)
    while(_i LESS _len)
        list(GET _qtgui_link_opts ${_i} _cur)
        if(_cur STREQUAL "-framework")
            math(EXPR _next "${_i}+1")
            if(_next LESS _len)
                list(GET _qtgui_link_opts ${_next} _fw)
                if(_fw STREQUAL "AGL" OR _fw STREQUAL "OpenGL")
                    # 跳过该对（-framework AGL/OpenGL）
                    math(EXPR _i "${_i}+2")
                    continue()
                endif()
            endif()
        endif()
        list(APPEND _filtered_qtgui_link_opts "${_cur}")
        math(EXPR _i "${_i}+1")
    endwhile()
    if(NOT _filtered_qtgui_link_opts STREQUAL _qtgui_link_opts)
        set_property(TARGET Qt6::Gui PROPERTY INTERFACE_LINK_OPTIONS "${_filtered_qtgui_link_opts}")
        message(STATUS "[Tests] Filtered Qt6::Gui INTERFACE_LINK_OPTIONS to remove -framework AGL/OpenGL")
    endif()
endif()


# 查找OpenSSL - 使用手动配置（与主CMakeLists.txt一致）
if(WIN32 AND EXISTS "C:/Program Files/OpenSSL-Win64")
    set(OPENSSL_ROOT_DIR "C:/Program Files/OpenSSL-Win64")
    set(OPENSSL_INCLUDE_DIR "C:/Program Files/OpenSSL-Win64/include")
    set(OPENSSL_CRYPTO_LIBRARY "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MD/libcrypto.lib")
    set(OPENSSL_SSL_LIBRARY "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MD/libssl.lib")
    set(OPENSSL_LIBRARIES "${OPENSSL_SSL_LIBRARY}" "${OPENSSL_CRYPTO_LIBRARY}")
    set(OPENSSL_FOUND TRUE)
    
    if(NOT TARGET OpenSSL::SSL)
        add_library(OpenSSL::SSL UNKNOWN IMPORTED)
        set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_SSL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
        )
    endif()
    
    if(NOT TARGET OpenSSL::Crypto)
        add_library(OpenSSL::Crypto UNKNOWN IMPORTED)
        set_target_properties(OpenSSL::Crypto PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
        )
    endif()
    
    message(STATUS "[Tests] Using manually configured OpenSSL")
else()
    find_package(OpenSSL REQUIRED)
endif()

# 查找ZLIB - 使用Qt的bundled zlib
if(TARGET Qt6::ZlibPrivate)
    message(STATUS "[Tests] Using Qt's bundled zlib")
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS Qt6::ZlibPrivate)
    endif()
    set(ZLIB_FOUND TRUE)
else()
    find_package(ZLIB REQUIRED)
endif()

# 设置Qt6自动处理
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/server
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/common/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/common/core/threading
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/common/core/adaptive
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 创建构建目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../build/test)

# 启用测试
enable_testing()

# ThreadManager测试用例
set(THREADMANAGER_TEST_SOURCES
    test_threadmanager.cpp
    
    # ThreadManager相关源文件
    ../src/common/core/threading/ThreadManager.cpp
    ../src/common/core/threading/Worker.cpp
    ../src/common/core/threading/PerformanceOptimizer.cpp
    
    # 公共核心源文件
    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/config/Config.cpp
    ../src/common/core/config/Constants.cpp
)

# 创建ThreadManager测试可执行文件
qt_add_executable(test_threadmanager
    ${THREADMANAGER_TEST_SOURCES}
)

# 链接库
# ThreadManager测试不依赖GUI，去除Qt6::Gui以避免不必要的系统框架链接
target_link_libraries(test_threadmanager PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Network
    Qt6::Concurrent
)

# 添加ThreadManager测试
add_test(
    NAME ThreadManagerTest
    COMMAND test_threadmanager
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# 设置ThreadManager测试属性
set_tests_properties(ThreadManagerTest PROPERTIES
    TIMEOUT 30  # 30秒超时
    LABELS "unit;threading;manager"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# ScreenCaptureWorker测试用例
set(SCREENCAPTUREWORKER_TEST_SOURCES
    test_screencaptureworker.cpp
    
    # ScreenCaptureWorker相关源文件
    ../src/server/capture/ScreenCaptureWorker.cpp
    ../src/server/dataprocessing/DataProcessing.cpp

    # 数据流相关源文件
    ../src/server/dataflow/QueueManager.cpp
    ../src/server/dataflow/DataFlowStructures.cpp

    ../src/common/core/threading/Worker.cpp
    ../src/common/core/threading/ThreadManager.cpp
    ../src/common/core/threading/PerformanceOptimizer.cpp
    
    # 公共核心源文件
    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/config/Config.cpp
    ../src/common/core/config/Constants.cpp
)

# 创建ScreenCaptureWorker测试可执行文件
qt_add_executable(test_screencaptureworker
    ${SCREENCAPTUREWORKER_TEST_SOURCES}
)

# 链接库
# ScreenCaptureWorker测试使用QImage等GUI类型，保留Gui模块
# 同时禁用OpenGL相关代码路径，防止Qt尝试链接OpenGL/AGL
target_link_libraries(test_screencaptureworker PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Gui
    Qt6::Network
    Qt6::Concurrent
)
target_compile_definitions(test_screencaptureworker PRIVATE QT_NO_OPENGL)

# 添加ScreenCaptureWorker测试
add_test(
    NAME ScreenCaptureWorkerTest
    COMMAND test_screencaptureworker
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# 设置ScreenCaptureWorker测试属性
set_tests_properties(ScreenCaptureWorkerTest PROPERTIES
    TIMEOUT 30  # 30秒超时
    LABELS "unit;capture;worker"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# 创建自定义目标来运行所有测试
add_custom_target(run_all_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running all tests"
)

# 创建快速测试目标（排除长时间运行的测试）
add_custom_target(run_quick_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L unit -E ScreenCapture
    COMMENT "Running quick tests (excluding stress and performance tests)"
)

# 创建内存相关测试目标
add_custom_target(run_memory_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "memory"
    COMMENT "Running memory-related tests"
)

# 创建核心组件测试目标
add_custom_target(run_core_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "unit"
    COMMENT "Running core component tests"
)

# 创建线程相关测试目标
add_custom_target(run_threading_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "threading"
    COMMENT "Running threading-related tests"
)

# 创建性能相关测试目标
add_custom_target(run_performance_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "performance"
    DEPENDS 
    COMMENT "Running performance-related tests"
)

# ScreenCapture主类测试用例
set(SCREENCAPTURE_TEST_SOURCES
    test_screencapture.cpp
    
    # ScreenCapture相关源文件
    ../src/server/capture/ScreenCapture.cpp
    ../src/server/capture/ScreenCaptureWorker.cpp
    ../src/server/dataprocessing/DataProcessing.cpp

    # 数据流相关源文件
    ../src/server/dataflow/QueueManager.cpp
    ../src/server/dataflow/DataFlowStructures.cpp

    ../src/common/core/threading/Worker.cpp
    ../src/common/core/threading/ThreadManager.cpp
    ../src/common/core/threading/PerformanceOptimizer.cpp
    
    # 公共核心源文件
    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/config/Config.cpp
    ../src/common/core/config/Constants.cpp
)

# 创建ScreenCapture测试可执行文件
qt_add_executable(test_screencapture
    ${SCREENCAPTURE_TEST_SOURCES}
)

# 链接库（ScreenCapture 需要Gui）
target_link_libraries(test_screencapture PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Gui
    Qt6::Network
    Qt6::Concurrent
)
target_compile_definitions(test_screencapture PRIVATE QT_NO_OPENGL)

# 添加ScreenCapture测试
add_test(
    NAME ScreenCaptureTest
    COMMAND test_screencapture
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# 设置ScreenCapture测试属性
set_tests_properties(ScreenCaptureTest PROPERTIES
    TIMEOUT 60  # 1分钟超时
    LABELS "unit;capture;main"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)


# --- 保证自定义测试目标的依赖是“目标”而非“文件路径” ---
# 注意：当使用add_custom_target(DEPENDS ...)且依赖的目标尚未定义时，
# 某些生成器可能将其解析为输出文件路径，最终引发“No rule to make target”错误。
# 因此在所有测试可执行目标创建完毕后，使用add_dependencies进行明确的目标依赖。
if(TARGET run_all_tests)
    add_dependencies(run_all_tests test_threadmanager test_screencaptureworker test_screencapture)
endif()
if(TARGET run_quick_tests)
    add_dependencies(run_quick_tests test_threadmanager)
endif()
if(TARGET run_core_tests)
    add_dependencies(run_core_tests test_threadmanager test_screencaptureworker test_screencapture)
endif()
if(TARGET run_threading_tests)
    add_dependencies(run_threading_tests test_threadmanager test_screencaptureworker)
endif()

# ScreenCapture集成测试用例
set(SCREEN_CAPTURE_INTEGRATION_TEST_SOURCES
    test_screen_capture_integration.cpp
    
    # ScreenCapture相关源文件
    ../src/server/capture/ScreenCapture.cpp
    ../src/server/capture/ScreenCaptureWorker.cpp
    ../src/server/dataprocessing/DataProcessing.cpp

    # 数据流相关源文件
    ../src/server/dataflow/QueueManager.cpp
    ../src/server/dataflow/DataFlowStructures.cpp
    
    # ThreadManager相关源文件
    ../src/common/core/threading/ThreadManager.cpp
    ../src/common/core/threading/Worker.cpp
    ../src/common/core/threading/ThreadCommunication.cpp
    
    # 公共核心源文件
    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/config/Config.cpp
    ../src/common/core/config/Constants.cpp
)

# 创建ScreenCapture集成测试可执行文件
qt_add_executable(test_screen_capture_integration
    ${SCREEN_CAPTURE_INTEGRATION_TEST_SOURCES}
)

# 链接库
target_link_libraries(test_screen_capture_integration PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Gui
    Qt6::Network
    Qt6::Concurrent
)

# 添加ScreenCapture集成测试
add_test(
    NAME ScreenCaptureIntegrationTest
    COMMAND test_screen_capture_integration
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# 设置ScreenCapture集成测试属性
set_tests_properties(ScreenCaptureIntegrationTest PROPERTIES
    TIMEOUT 300  # 5分钟超时
    LABELS "integration;screencapture;redundancy"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

message(STATUS "  Tests: Memory Optimization, ThreadManager, ScreenCaptureWorker, PerformanceOptimizer, ScreenCaptureIntegration")
message(STATUS "  Test targets:")
message(STATUS "    - run_all_tests: 运行所有测试")
message(STATUS "    - run_quick_tests: 运行快速测试")
message(STATUS "    - run_core_tests: 运行核心组件测试")
message(STATUS "    - run_memory_tests: 运行内存相关测试")
message(STATUS "    - run_threading_tests: 运行线程相关测试")
message(STATUS "    - run_performance_tests: 运行性能相关测试")
message(STATUS "    - run_worker_tests: 运行工作线程测试")

# 数据处理模块测试用例
set(DATAPROCESSING_TEST_SOURCES
    test_dataprocessing.cpp

    # 数据处理模块源文件
    ../src/server/dataprocessing/DataProcessing.cpp

    # 公共核心源文件（日志分类）
    ../src/common/core/logging/LoggingCategories.cpp
)

# 创建数据处理模块测试可执行文件
qt_add_executable(test_dataprocessing
    ${DATAPROCESSING_TEST_SOURCES}
)

# 链接库
# 说明：DataProcessing 使用到 QImage/QImageReader，必须链接 Qt6::Gui
#       并使用 Qt6::Test 作为测试框架
#       不链接OpenGL以避免AGL相关问题
target_link_libraries(test_dataprocessing PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Gui
)

target_compile_definitions(test_dataprocessing PRIVATE QT_NO_OPENGL)

# 添加测试
add_test(NAME DataProcessingTest COMMAND test_dataprocessing)
set_tests_properties(DataProcessingTest PROPERTIES LABELS "unit;server;dataprocessing")



# 数据一致性验证测试用例
set(DATA_CONSISTENCY_TEST_SOURCES
    test_data_consistency.cpp
    ../src/server/dataprocessing/DataProcessing.cpp
    ../src/server/dataprocessing/DataProcessingConfig.cpp

    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/network/ProtocolImpl.cpp
    ../src/common/core/threading/Worker.cpp
    ../src/common/core/threading/ThreadManager.cpp
)

qt_add_executable(test_data_consistency
    ${DATA_CONSISTENCY_TEST_SOURCES}
)

target_link_libraries(test_data_consistency PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Gui
    Qt6::Network
)

target_compile_definitions(test_data_consistency PRIVATE QT_NO_OPENGL)

# 添加测试
add_test(NAME DataConsistencyTest COMMAND test_data_consistency)
set_tests_properties(DataConsistencyTest PROPERTIES LABELS "integration;consistency;network")

# 帧传输延迟测试用例
set(FRAME_TRANSMISSION_LATENCY_TEST_SOURCES
    test_frame_transmission_latency.cpp
    ../src/server/dataprocessing/DataProcessing.cpp
    ../src/server/dataprocessing/DataProcessingConfig.cpp

    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/network/ProtocolImpl.cpp
    ../src/common/core/threading/Worker.cpp
    ../src/common/core/threading/ThreadManager.cpp
)

qt_add_executable(test_frame_transmission_latency
    ${FRAME_TRANSMISSION_LATENCY_TEST_SOURCES}
)

target_link_libraries(test_frame_transmission_latency PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Gui
    Qt6::Network
)

target_compile_definitions(test_frame_transmission_latency PRIVATE QT_NO_OPENGL)

# 添加测试
add_test(NAME FrameTransmissionLatencyTest COMMAND test_frame_transmission_latency)
set_tests_properties(FrameTransmissionLatencyTest PROPERTIES LABELS "performance;latency;network")

# 保证自定义测试目标的依赖
if(TARGET run_all_tests)
    add_dependencies(run_all_tests test_dataprocessing)
endif()
if(TARGET run_core_tests)
    add_dependencies(run_core_tests test_dataprocessing)
endif()

# ClientRemoteWindow测试用例
set(CLIENTREMOTEWINDOW_TEST_SOURCES
    test_clientremotewindow.cpp
    
    # ClientRemoteWindow相关源文件
    ../src/client/window/ClientRemoteWindow.cpp
    ../src/client/managers/FileTransferManager.cpp
    ../src/client/window/RenderManager.cpp
    ../src/client/managers/SessionManager.cpp
    ../src/client/network/ConnectionManager.cpp
    ../src/client/network/TcpClient.cpp
    
    # 公共核心源文件
    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/config/Constants.cpp
    ../src/common/core/config/Config.cpp
    ../src/common/core/network/ProtocolImpl.cpp
    ../src/common/core/crypto/Encryption.cpp
)

# 创建ClientRemoteWindow测试可执行文件
qt_add_executable(test_clientremotewindow
    ${CLIENTREMOTEWINDOW_TEST_SOURCES}
)

# 链接库
# ClientRemoteWindow测试需要GUI组件，因为它是QGraphicsView的子类
target_link_libraries(test_clientremotewindow PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
)

target_compile_definitions(test_clientremotewindow PRIVATE QT_NO_OPENGL)

# 添加ClientRemoteWindow测试
add_test(
    NAME ClientRemoteWindowTest
    COMMAND test_clientremotewindow
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# 设置ClientRemoteWindow测试属性
set_tests_properties(ClientRemoteWindowTest PROPERTIES
    TIMEOUT 30  # 30秒超时
    LABELS "unit;client;window"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# 保证自定义测试目标的依赖
if(TARGET run_all_tests)
    add_dependencies(run_all_tests test_clientremotewindow)
endif()
if(TARGET run_core_tests)
    add_dependencies(run_core_tests test_clientremotewindow)
endif()

# 屏幕数据流程测试用例
set(SCREEN_DATA_FLOW_TEST_SOURCES
    test_screen_data_flow.cpp
    
    # 只包含必要的核心源文件
    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/network/ProtocolImpl.cpp
)

# 创建屏幕数据流程测试可执行文件
qt_add_executable(test_screen_data_flow
    ${SCREEN_DATA_FLOW_TEST_SOURCES}
)

# 链接库
# 屏幕数据流程测试需要完整的GUI和网络组件
target_link_libraries(test_screen_data_flow PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
)

target_compile_definitions(test_screen_data_flow PRIVATE QT_NO_OPENGL)

# 添加屏幕数据流程测试
add_test(
    NAME ScreenDataFlowTest
    COMMAND test_screen_data_flow
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# 设置屏幕数据流程测试属性
set_tests_properties(ScreenDataFlowTest PROPERTIES
    TIMEOUT 120  # 2分钟超时
    LABELS "integration;screen;data;flow;network"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# 保证自定义测试目标的依赖
if(TARGET run_all_tests)
    add_dependencies(run_all_tests test_screen_data_flow)
endif()
if(TARGET run_integration_tests)
    add_dependencies(run_integration_tests test_screen_data_flow)
endif()

# ClientHandlerWorker测试用例
set(CLIENTHANDLERWORKER_TEST_SOURCES
    test_clienthandlerworker.cpp
    
    # 基本核心源文件
    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/config/Config.cpp
    ../src/common/core/config/Constants.cpp
)

# 创建ClientHandlerWorker测试可执行文件
qt_add_executable(test_clienthandlerworker
    ${CLIENTHANDLERWORKER_TEST_SOURCES}
)

# 链接库
target_link_libraries(test_clienthandlerworker PRIVATE
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(test_clienthandlerworker PRIVATE QT_NO_OPENGL)

# 添加ClientHandlerWorker测试
add_test(
    NAME ClientHandlerWorkerTest
    COMMAND test_clienthandlerworker
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# 设置ClientHandlerWorker测试属性
set_tests_properties(ClientHandlerWorkerTest PROPERTIES
    TIMEOUT 60  # 1分钟超时
    LABELS "unit;server;worker;network"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# 保证自定义测试目标的依赖
if(TARGET run_all_tests)
    add_dependencies(run_all_tests test_clienthandlerworker)
endif()
if(TARGET run_unit_tests)
    add_dependencies(run_unit_tests test_clienthandlerworker)
endif()

# ============================================================================
# 生产者-消费者集成测试
# ============================================================================

# 生产者-消费者集成测试源文件
set(PRODUCER_CONSUMER_INTEGRATION_TEST_SOURCES
    test_producer_consumer_integration.cpp
    
    # 服务器相关源文件
    ../src/server/service/ServerWorker.cpp
    ../src/server/dataprocessing/DataProcessingWorker.cpp
    ../src/server/dataprocessing/DataProcessing.cpp
    ../src/server/dataprocessing/DataProcessingConfig.cpp
    ../src/server/capture/ScreenCapture.cpp
    ../src/server/capture/ScreenCaptureWorker.cpp
    ../src/server/clienthandler/ClientHandlerWorker.cpp
    ../src/server/service/TcpServer.cpp
    ../src/server/simulator/InputSimulator.cpp
    ../src/server/simulator/MouseSimulator.cpp
    ../src/server/simulator/KeyboardSimulator.cpp
)

# 添加平台特定的模拟器源文件
if(APPLE)
    list(APPEND PRODUCER_CONSUMER_INTEGRATION_TEST_SOURCES
        ../src/server/simulator/MouseSimulatorMacOS.cpp
        ../src/server/simulator/KeyboardSimulatorMacOS.cpp
    )
elseif(WIN32)
    list(APPEND PRODUCER_CONSUMER_INTEGRATION_TEST_SOURCES
        ../src/server/simulator/MouseSimulatorWindows.cpp
        ../src/server/simulator/KeyboardSimulatorWindows.cpp
    )
elseif(UNIX)
    list(APPEND PRODUCER_CONSUMER_INTEGRATION_TEST_SOURCES
        ../src/server/simulator/MouseSimulatorLinux.cpp
        ../src/server/simulator/KeyboardSimulatorLinux.cpp
    )
endif()

list(APPEND PRODUCER_CONSUMER_INTEGRATION_TEST_SOURCES
    
    # 公共核心源文件
    ../src/common/core/threading/Worker.cpp
    ../src/common/core/threading/ThreadManager.cpp
    ../src/server/dataflow/QueueManager.cpp
    ../src/common/core/network/ProtocolImpl.cpp
    ../src/common/core/crypto/Encryption.cpp
    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/config/Constants.cpp
)

# 创建生产者-消费者集成测试可执行文件
qt_add_executable(test_producer_consumer_integration
    ${PRODUCER_CONSUMER_INTEGRATION_TEST_SOURCES}
)

# 链接库
target_link_libraries(test_producer_consumer_integration PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Gui
    Qt6::Network
    Qt6::Concurrent
    OpenSSL::SSL
    OpenSSL::Crypto
    z  # zlib库
)

target_compile_definitions(test_producer_consumer_integration PRIVATE QT_NO_OPENGL)

# 添加生产者-消费者集成测试
add_test(
    NAME ProducerConsumerIntegrationTest
    COMMAND test_producer_consumer_integration
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# 设置生产者-消费者集成测试属性
set_tests_properties(ProducerConsumerIntegrationTest PROPERTIES
    TIMEOUT 300  # 5分钟超时
    LABELS "integration;producer;consumer;queue;threading"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# 保证自定义测试目标的依赖
if(TARGET run_all_tests)
    add_dependencies(run_all_tests test_producer_consumer_integration)
endif()
if(TARGET run_integration_tests)
    add_dependencies(run_integration_tests test_producer_consumer_integration)
endif()

# 关闭事件集成测试工具（GUI）
set(CLOSE_EVENT_TEST_SOURCES
    test_close_event.cpp
)

qt_add_executable(test_close_event
    ${CLOSE_EVENT_TEST_SOURCES}
)

target_link_libraries(test_close_event PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Test
    Qt6::Concurrent
)

target_compile_definitions(test_close_event PRIVATE QT_NO_OPENGL)

# 将关闭事件测试纳入CTest，允许在CI中运行
add_test(
    NAME CloseEventTest
    COMMAND test_close_event
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

set_tests_properties(CloseEventTest PROPERTIES
    TIMEOUT 60
    LABELS "integration;ui;closeevent;app"
    # 指定主应用可执行文件路径，避免测试误用旧版本的.app bundle
    # 这里使用主工程在Debug配置下的输出位置（见顶层CMakeLists: CMAKE_RUNTIME_OUTPUT_DIRECTORY 设置）
    # 对于非Debug配置，路径可根据需要扩展；当前测试默认在Debug下运行
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;AUTO_RUN=1;RD_MAIN_APP_PATH=${CMAKE_SOURCE_DIR}/Debug/QtRemoteDesktop.app/Contents/MacOS/QtRemoteDesktop"
)

