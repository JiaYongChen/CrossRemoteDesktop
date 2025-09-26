cmake_minimum_required(VERSION 3.16)

# 设置项目名称
project(QtRemoteDesktopTests)

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


# 查找OpenSSL
find_package(OpenSSL REQUIRED)

# 查找ZLIB
find_package(ZLIB REQUIRED)

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

# 高级压缩管理器测试用例
set(ADVANCED_COMPRESSION_MANAGER_TEST_SOURCES
    test_advancedcompressionmanager.cpp
    
    # 高级压缩管理器源文件
    ../src/common/core/compression/AdvancedCompressionManager.cpp
    
    # 压缩相关源文件
    ../src/common/core/compression/Compression.cpp
    
    # 公共核心源文件
    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/config/Constants.cpp
)

# 创建高级压缩管理器测试可执行文件
qt_add_executable(test_advancedcompressionmanager
    ${ADVANCED_COMPRESSION_MANAGER_TEST_SOURCES}
)

# 链接库
# 高级压缩管理器测试需要使用QImage/QPixmap（Compression.cpp中），因此必须链接Qt6::Gui
# 注意：仅链接Qt6::Gui，不链接OpenGL/OpenGLWidgets，避免引入AGL
target_link_libraries(test_advancedcompressionmanager PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Gui
    Qt6::Network
    Qt6::Concurrent
    z  # zlib库
)
# 强制禁用OpenGL路径，防止QtGui尝试加载OpenGL相关插件/符号
# 这在macOS arm64下可避免AGL符号引入造成的不匹配
# 参考：test_compression_features已设置QT_NO_OPENGL
# 且本测试并未使用任何OpenGL API，仅需图像处理能力

# 添加QT_NO_OPENGL定义
# 注：保持与工程其它测试一致的做法
# 我们在编译阶段禁止OpenGL相关代码路径
# 从而避免链接/运行时的OpenGL依赖
#
# 如果未来需要使用OpenGL，请去除此定义，并显式链接所需模块
#（同时确保目标平台支持）
target_compile_definitions(test_advancedcompressionmanager PRIVATE QT_NO_OPENGL)

# 添加高级压缩管理器测试
add_test(
    NAME AdvancedCompressionManagerTest
    COMMAND test_advancedcompressionmanager
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# 设置高级压缩管理器测试属性
set_tests_properties(AdvancedCompressionManagerTest PROPERTIES
    TIMEOUT 180  # 3分钟超时
    LABELS "unit;compression;advanced;optimization"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

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
    ../src/server/dataprocessing/StorageManager.cpp
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
    ../src/server/dataprocessing/StorageManager.cpp
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

# 创建压缩功能测试可执行文件
qt_add_executable(test_compression_features
    test_compression_features.cpp
    
    # 压缩相关源文件
    ../src/common/core/compression/Compression.cpp

    # 公共核心源文件
    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/config/Constants.cpp
)

# 链接库
# 压缩特性测试同样依赖Compression.cpp中的QImage/QPixmap，必须链接Qt6::Gui
# 注意：仅链接Qt6::Gui，不链接OpenGL/OpenGLWidgets，避免引入AGL
target_link_libraries(test_compression_features PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Gui
    ZLIB::ZLIB
)
target_compile_definitions(test_compression_features PRIVATE QT_NO_OPENGL)

# 设置包含目录
target_include_directories(test_compression_features PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common/core
)

# 添加LZ4和ZSTD支持（如果可用）
if(ENABLE_LZ4)
    find_library(LZ4_LIBRARY NAMES lz4 liblz4 PATHS /opt/homebrew/lib /usr/local/lib)
    find_path(LZ4_INCLUDE_DIR NAMES lz4.h PATHS /opt/homebrew/include /usr/local/include)
    if(LZ4_LIBRARY AND LZ4_INCLUDE_DIR)
        target_link_libraries(test_compression_features PRIVATE ${LZ4_LIBRARY})
        target_include_directories(test_compression_features PRIVATE ${LZ4_INCLUDE_DIR})
        target_compile_definitions(test_compression_features PRIVATE HAVE_LZ4)
    endif()
endif()

if(ENABLE_ZSTD)
    find_library(ZSTD_LIBRARY NAMES zstd libzstd PATHS /opt/homebrew/lib /usr/local/lib)
    find_path(ZSTD_INCLUDE_DIR NAMES zstd.h PATHS /opt/homebrew/include /usr/local/include)
    if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
        target_link_libraries(test_compression_features PRIVATE ${ZSTD_LIBRARY})
        target_include_directories(test_compression_features PRIVATE ${ZSTD_INCLUDE_DIR})
        target_compile_definitions(test_compression_features PRIVATE HAVE_ZSTD)
    endif()
endif()

# 添加测试
add_test(NAME CompressionFeatures COMMAND test_compression_features)
set_tests_properties(CompressionFeatures PROPERTIES LABELS "unit;compression")

# --- 保证自定义测试目标的依赖是“目标”而非“文件路径” ---
# 注意：当使用add_custom_target(DEPENDS ...)且依赖的目标尚未定义时，
# 某些生成器可能将其解析为输出文件路径，最终引发“No rule to make target”错误。
# 因此在所有测试可执行目标创建完毕后，使用add_dependencies进行明确的目标依赖。
if(TARGET run_all_tests)
    add_dependencies(run_all_tests test_advancedcompressionmanager test_threadmanager test_screencaptureworker test_screencapture test_compression_features)
endif()
if(TARGET run_quick_tests)
    add_dependencies(run_quick_tests test_advancedcompressionmanager test_threadmanager test_compression_features)
endif()
if(TARGET run_core_tests)
    add_dependencies(run_core_tests test_advancedcompressionmanager test_threadmanager test_screencaptureworker test_screencapture test_compression_features)
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
    ../src/server/dataprocessing/StorageManager.cpp
    
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

message(STATUS "  Tests: Memory Optimization, Advanced Compression, ThreadManager, ScreenCaptureWorker, PerformanceOptimizer, CompressionFeatures, ScreenCaptureIntegration")
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
    ../src/common/data/DataRecord.h

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

# StorageManager测试用例
set(STORAGEMANAGER_TEST_SOURCES
    test_storagemanager.cpp
    ../src/server/dataprocessing/StorageManager.cpp
    ../src/common/data/DataRecord.h
    ../src/common/core/logging/LoggingCategories.cpp
)

qt_add_executable(test_storagemanager
    ${STORAGEMANAGER_TEST_SOURCES}
)

target_link_libraries(test_storagemanager PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Gui
)

target_compile_definitions(test_storagemanager PRIVATE QT_NO_OPENGL)

# 添加测试
add_test(NAME StorageManagerTest COMMAND test_storagemanager)
set_tests_properties(StorageManagerTest PROPERTIES LABELS "unit;server;storage")

# 数据一致性验证测试用例
set(DATA_CONSISTENCY_TEST_SOURCES
    test_data_consistency.cpp
    ../src/server/dataprocessing/DataProcessing.cpp
    ../src/server/dataprocessing/DataProcessingConfig.cpp
    ../src/server/dataprocessing/StorageManager.cpp
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
    ../src/server/dataprocessing/StorageManager.cpp
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
    ../src/client/ClientRemoteWindow.cpp
    ../src/client/InputHandler.cpp
    ../src/client/managers/ClipboardManager.cpp
    ../src/client/managers/FileTransferManager.cpp
    ../src/client/managers/CursorManager.cpp
    ../src/client/managers/RenderManager.cpp
    ../src/client/managers/SessionManager.cpp
    ../src/client/managers/ConnectionManager.cpp
    ../src/client/TcpClient.cpp
    
    # 公共核心源文件
    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/config/Constants.cpp
    ../src/common/core/config/Config.cpp
    ../src/common/core/network/ProtocolImpl.cpp
    ../src/common/core/crypto/Encryption.cpp
    
    # 压缩相关源文件
    ../src/common/core/compression/Compression.cpp
    ../src/common/core/compression/DifferentialCompression.cpp
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