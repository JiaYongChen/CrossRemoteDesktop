cmake_minimum_required(VERSION 3.16)

project(CrossRemoteDesktopTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── 编译选项 ──
if(MSVC)
    add_compile_options(/W4 /WX /FS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-c++20-attribute-extensions)
endif()

# ── Qt6 + OpenSSL ──
if(NOT TARGET Qt6::Core)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Core Network Test Gui Concurrent)
endif()
if(NOT TARGET Qt6::Test)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Test)
endif()
include(../cmake/FilterAppleOpenGL.cmake)
rd_filter_apple_opengl(Qt6::Gui)

if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
    message(STATUS "[Tests] Reusing OpenSSL targets from parent project")
else()
    find_package(OpenSSL REQUIRED)
endif()

# ── 自动工具 + 包含目录 + 输出 ──
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/server
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/common/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/common/core/threading
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../build/test)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../build/test)
enable_testing()

# ── Windows 运行时 PATH 配置 ──
if(WIN32 AND QT6_ROOT_DIR)
    set(_qt_bindir "${QT6_ROOT_DIR}/bin")
    set(_qt_plugindir "${QT6_ROOT_DIR}/plugins")

    set(_test_path_value "${_qt_bindir}")
    if(OPENSSL_TP_BIN)
        string(APPEND _test_path_value "\;${OPENSSL_TP_BIN}")
    endif()
    string(REPLACE ";" "\;" _sys_path_escaped "$ENV{PATH}")
    string(APPEND _test_path_value "\;${_sys_path_escaped}")

    set(_TEST_BASE_ENV
        "QT_QPA_PLATFORM=offscreen"
        "PATH=${_test_path_value}"
        "QT_PLUGIN_PATH=${_qt_plugindir}"
    )
    message(STATUS "[Tests] Windows runtime PATH configured (Qt: ${_qt_bindir})")
else()
    set(_TEST_BASE_ENV "QT_QPA_PLATFORM=offscreen")
endif()

# ── 共享静态库 ──
qt_add_library(common_test_core STATIC
    ../src/common/core/logging/LoggingCategories.cpp
    ../src/common/core/config/Config.cpp
    ../src/common/core/config/Constants.cpp
)
target_link_libraries(common_test_core PUBLIC Qt6::Core)

qt_add_library(threading_test_core STATIC
    ../src/common/core/threading/Worker.cpp
    ../src/common/core/threading/ThreadManager.cpp
)
target_link_libraries(threading_test_core PUBLIC Qt6::Core common_test_core)

qt_add_library(capture_test_core STATIC
    ../src/server/capture/ScreenCaptureWorker.cpp
    ../src/server/dataflow/QueueManager.cpp
)
if(WIN32)
    target_sources(capture_test_core PRIVATE ../src/server/capture/DxgiCapture.cpp)
endif()
target_link_libraries(capture_test_core PUBLIC Qt6::Core Qt6::Gui threading_test_core)
if(WIN32)
    target_link_libraries(capture_test_core PUBLIC d3d11 dxgi)
endif()

# ── 聚合测试目标 ──
include(../cmake/TestHelpers.cmake)

add_custom_target(run_all_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running all tests"
)
add_custom_target(run_quick_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L unit -E ScreenCapture
    COMMENT "Running quick tests (excluding stress and performance tests)"
)
add_custom_target(run_core_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "unit"
    COMMENT "Running core component tests"
)
add_custom_target(run_threading_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "threading"
    COMMENT "Running threading-related tests"
)
add_custom_target(run_performance_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "performance"
    COMMENT "Running performance-related tests"
)
add_custom_target(run_integration_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "integration"
    COMMENT "Running integration tests"
)

# ═══════════════════════════════════════════════════════════════════════════════
# 测试定义（22 个目标，使用 add_rd_test() 辅助函数）
# ═══════════════════════════════════════════════════════════════════════════════

# unit / threading
add_rd_test(TARGET test_threadmanager  NAME ThreadManagerTest
    SOURCES test_threadmanager.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Network Qt6::Concurrent threading_test_core
    TIMEOUT 30  LABELS unit threading manager)

# unit / capture
add_rd_test(TARGET test_screencaptureworker  NAME ScreenCaptureWorkerTest
    SOURCES test_screencaptureworker.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Network Qt6::Concurrent capture_test_core
    TIMEOUT 30  LABELS unit capture worker  NO_OPENGL)

add_rd_test(TARGET test_screencapture  NAME ScreenCaptureTest
    SOURCES test_screencapture.cpp
    EXTRA_SOURCES ../src/server/capture/ScreenCapture.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Network Qt6::Concurrent capture_test_core
    TIMEOUT 60  LABELS unit capture main  NO_OPENGL)

add_rd_test(TARGET test_dxgi_capture  NAME DxgiCaptureTest
    SOURCES test_dxgi_capture.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui common_test_core
    TIMEOUT 30  LABELS unit capture dxgi windows  NO_OPENGL)
if(WIN32)
    target_sources(test_dxgi_capture PRIVATE ../src/server/capture/DxgiCapture.cpp)
    target_link_libraries(test_dxgi_capture PRIVATE d3d11 dxgi)
endif()

# unit / server
add_rd_test(TARGET test_queuemanager  NAME QueueManagerTest
    SOURCES test_queuemanager.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::Network capture_test_core
    TIMEOUT 30  LABELS unit server queue  NO_OPENGL)

add_rd_test(TARGET test_dataprocessing  NAME DataProcessingTest
    SOURCES test_dataprocessing.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui common_test_core
    TIMEOUT 30  LABELS unit server dataprocessing  NO_OPENGL)

# unit / dataflow
add_rd_test(TARGET test_captured_frame  NAME CapturedFrameTest
    SOURCES test_captured_frame.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui common_test_core
    TIMEOUT 30  LABELS unit server dataflow  NO_OPENGL)

# unit / client
add_rd_test(TARGET test_refresh_latency_metric  NAME RefreshLatencyMetricTest
    SOURCES test_refresh_latency_metric.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui common_test_core
    TIMEOUT 30  LABELS unit client metrics  NO_OPENGL)

add_rd_test(TARGET test_session_latest_wins  NAME SessionLatestWinsTest
    SOURCES test_session_latest_wins.cpp
    EXTRA_SOURCES
        ../src/client/managers/SessionManager.cpp
        ../src/client/managers/DecodeWorker.cpp
        ../src/client/network/ConnectionManager.cpp
        ../src/client/network/TcpClient.cpp
        ../src/common/core/network/ProtocolImpl.cpp
        ../src/common/core/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Network common_test_core OpenSSL::SSL OpenSSL::Crypto
    TIMEOUT 30  LABELS unit client queue  NO_OPENGL)

add_rd_test(TARGET test_triple_buffer_swap  NAME TripleBufferSwapTest
    SOURCES test_triple_buffer_swap.cpp
    LIBRARIES Qt6::Core Qt6::Test
    TIMEOUT 30  LABELS unit client threading)

# unit / render + GL
add_rd_test(TARGET test_render_config  NAME RenderConfigTest
    SOURCES test_render_config.cpp
    LIBRARIES Qt6::Core Qt6::Test
    TIMEOUT 30  LABELS unit config)

add_rd_test(TARGET test_render_surface  NAME RenderSurfaceTest
    SOURCES test_render_surface.cpp
    EXTRA_SOURCES ../src/client/window/GLTextureViewport.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets common_test_core
    TIMEOUT 30  LABELS unit client render)

add_rd_test(TARGET test_gl_upload_formats  NAME GLUploadFormatsTest
    SOURCES test_gl_upload_formats.cpp
    EXTRA_SOURCES ../src/client/window/GLTextureViewport.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets common_test_core
    TIMEOUT 30  LABELS unit client gl)

add_rd_test(TARGET test_gl_worker_upload  NAME GLWorkerUploadTest
    SOURCES test_gl_worker_upload.cpp
    EXTRA_SOURCES ../src/client/window/GLTextureViewport.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets common_test_core
    TIMEOUT 30  LABELS unit client gl)

# unit / client window
add_rd_test(TARGET test_clientremotewindow  NAME ClientRemoteWindowTest
    SOURCES test_clientremotewindow.cpp
    EXTRA_SOURCES
        ../src/client/window/ClientRemoteWindow.cpp
        ../src/client/window/InputForwarder.cpp
        ../src/client/window/ConnectionLifecycle.cpp
        ../src/client/window/CursorManager.cpp
        ../src/client/window/RenderManager.cpp
        ../src/client/managers/SessionManager.cpp
        ../src/client/managers/DecodeWorker.cpp
        ../src/client/network/ConnectionManager.cpp
        ../src/client/network/TcpClient.cpp
        ../src/common/core/network/ProtocolImpl.cpp
        ../src/common/core/network/MessageCodec.cpp
        ../src/common/clipboard/ClipboardManager.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::Network OpenSSL::SSL OpenSSL::Crypto common_test_core
    TIMEOUT 30  LABELS unit client window  NO_OPENGL)

# unit / server worker
add_rd_test(TARGET test_clienthandlerworker  NAME ClientHandlerWorkerTest
    SOURCES test_clienthandlerworker.cpp
    LIBRARIES Qt6::Core Qt6::Test common_test_core
    TIMEOUT 60  LABELS unit server worker network  NO_OPENGL)

# integration
add_rd_test(TARGET test_screen_capture_integration  NAME ScreenCaptureIntegrationTest
    SOURCES test_screen_capture_integration.cpp
    EXTRA_SOURCES ../src/server/capture/ScreenCapture.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Network Qt6::Concurrent capture_test_core
    TIMEOUT 300  LABELS integration screencapture redundancy  NO_OPENGL)

add_rd_test(TARGET test_data_consistency  NAME DataConsistencyTest
    SOURCES test_data_consistency.cpp
    EXTRA_SOURCES ../src/common/core/network/ProtocolImpl.cpp ../src/common/core/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Network threading_test_core
    TIMEOUT 30  LABELS integration consistency network  NO_OPENGL)

add_rd_test(TARGET test_screen_data_flow  NAME ScreenDataFlowTest
    SOURCES test_screen_data_flow.cpp
    EXTRA_SOURCES ../src/common/core/network/ProtocolImpl.cpp ../src/common/core/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Concurrent OpenSSL::SSL OpenSSL::Crypto common_test_core
    TIMEOUT 120  LABELS integration screen data flow network  NO_OPENGL)

add_rd_test(TARGET test_producer_consumer_integration  NAME ProducerConsumerIntegrationTest
    SOURCES test_producer_consumer_integration.cpp
    EXTRA_SOURCES
        ../src/server/service/ServerWorker.cpp
        ../src/server/dataprocessing/DataProcessingWorker.cpp
        ../src/server/dataprocessing/DataProcessing.cpp
        ../src/server/capture/ScreenCapture.cpp
        ../src/server/clienthandler/ClientHandlerWorker.cpp
        ../src/server/clienthandler/AuthHandler.cpp
        ../src/server/service/TcpServer.cpp
        ../src/server/simulator/InputSimulator.cpp
        ../src/server/simulator/MouseSimulator.cpp
        ../src/server/simulator/KeyboardSimulator.cpp
        ../src/common/core/network/ProtocolImpl.cpp
        ../src/common/core/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Network Qt6::Concurrent OpenSSL::SSL OpenSSL::Crypto capture_test_core
    TIMEOUT 300  LABELS integration producer consumer queue threading  NO_OPENGL)
if(WIN32)
    target_sources(test_producer_consumer_integration PRIVATE
        ../src/server/simulator/MouseSimulatorWindows.cpp
        ../src/server/simulator/KeyboardSimulatorWindows.cpp)
elseif(APPLE)
    target_sources(test_producer_consumer_integration PRIVATE
        ../src/server/simulator/MouseSimulatorMacOS.cpp
        ../src/server/simulator/KeyboardSimulatorMacOS.cpp)
elseif(UNIX)
    target_sources(test_producer_consumer_integration PRIVATE
        ../src/server/simulator/MouseSimulatorLinux.cpp
        ../src/server/simulator/KeyboardSimulatorLinux.cpp)
endif()

# integration / UI（需额外环境变量）
add_rd_test(TARGET test_close_event  NAME CloseEventTest
    SOURCES test_close_event.cpp
    LIBRARIES Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test Qt6::Concurrent
    TIMEOUT 60  LABELS integration ui closeevent app  NO_OPENGL
    EXTRA_ENV "AUTO_RUN=1" "RD_MAIN_APP_PATH=${CMAKE_SOURCE_DIR}/Debug/CrossRemoteDesktop.app/Contents/MacOS/CrossRemoteDesktop")

# performance
add_rd_test(TARGET test_frame_transmission_latency  NAME FrameTransmissionLatencyTest
    SOURCES test_frame_transmission_latency.cpp
    EXTRA_SOURCES ../src/common/core/network/ProtocolImpl.cpp ../src/common/core/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Network threading_test_core
    TIMEOUT 30  LABELS performance latency network  NO_OPENGL)

# SessionManager 单元测试
add_rd_test(TARGET test_sessionmanager_logic  NAME SessionManagerLogicTest
    SOURCES test_sessionmanager_logic.cpp
    EXTRA_SOURCES
        ../src/client/managers/SessionManager.cpp
        ../src/client/managers/DecodeWorker.cpp
        ../src/client/network/ConnectionManager.cpp
        ../src/client/network/TcpClient.cpp
        ../src/common/core/network/ProtocolImpl.cpp
        ../src/common/core/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::Network common_test_core OpenSSL::SSL OpenSSL::Crypto
    TIMEOUT 30  LABELS unit client session  NO_OPENGL)

add_rd_test(TARGET test_sessionmanager_data  NAME SessionManagerDataTest
    SOURCES test_sessionmanager_data.cpp
    EXTRA_SOURCES
        ../src/client/managers/SessionManager.cpp
        ../src/client/managers/DecodeWorker.cpp
        ../src/client/network/ConnectionManager.cpp
        ../src/client/network/TcpClient.cpp
        ../src/common/core/network/ProtocolImpl.cpp
        ../src/common/core/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::Network common_test_core OpenSSL::SSL OpenSSL::Crypto
    TIMEOUT 30  LABELS unit client session data  NO_OPENGL)

add_rd_test(TARGET test_sessionmanager_lifecycle  NAME SessionManagerLifecycleTest
    SOURCES test_sessionmanager_lifecycle.cpp
    EXTRA_SOURCES
        ../src/client/managers/SessionManager.cpp
        ../src/client/managers/DecodeWorker.cpp
        ../src/client/network/ConnectionManager.cpp
        ../src/client/network/TcpClient.cpp
        ../src/common/core/network/ProtocolImpl.cpp
        ../src/common/core/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::Network common_test_core OpenSSL::SSL OpenSSL::Crypto
    TIMEOUT 30  LABELS unit client session lifecycle  NO_OPENGL)

# smoke / network components
add_rd_test(TARGET test_smoke_network  NAME SmokeNetworkTest
    SOURCES test_smoke_network.cpp
    EXTRA_SOURCES
        ../src/client/network/ConnectionManager.cpp
        ../src/client/network/TcpClient.cpp
        ../src/common/core/network/ProtocolImpl.cpp
        ../src/common/core/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Network common_test_core
    TIMEOUT 30  LABELS smoke network unit  NO_OPENGL)
