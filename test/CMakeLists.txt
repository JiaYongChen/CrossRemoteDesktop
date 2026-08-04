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
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${TURBOJPEG_INCLUDE_DIR}
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
    if(TURBOJPEG_TP_BIN)
        string(APPEND _test_path_value "\;${TURBOJPEG_TP_BIN}")
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
    ../src/common/logging/LoggingCategories.cpp
    ../src/common/config/SettingsManager.cpp
    ../src/common/config/ConnectionHistory.cpp
    ../src/common/crypto/PasswordCrypto.cpp
)
target_link_libraries(common_test_core PUBLIC Qt6::Core OpenSSL::SSL OpenSSL::Crypto)

qt_add_library(threading_test_core STATIC
    ../src/common/threading/Worker.cpp
    ../src/common/threading/ThreadManager.cpp
)
target_link_libraries(threading_test_core PUBLIC Qt6::Core common_test_core)

qt_add_library(capture_test_core STATIC
    ../src/server/capture/ScreenCaptureWorker.cpp
    ../src/server/capture/ScreenCaptureFactory.cpp
    ../src/server/dataflow/QueueManager.cpp
    ../src/common/network/ProtocolImpl.cpp
    ../src/common/network/MessageCodec.cpp
)
if(WIN32)
    target_sources(capture_test_core PRIVATE ../src/server/capture/windows/DxgiCapture.cpp)
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
# 测试定义（23 个目标，使用 add_rd_test() 辅助函数）
# ═══════════════════════════════════════════════════════════════════════════════

# unit / threading
add_rd_test(TARGET test_threadmanager  NAME ThreadManagerTest
    SOURCES common/threading/test_threadmanager.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Network Qt6::Concurrent threading_test_core
    TIMEOUT 30  LABELS unit threading manager)

# unit / config
add_rd_test(TARGET test_settingsmanager_migration  NAME SettingsManagerMigrationTest
    SOURCES common/config/test_settingsmanager_migration.cpp
    LIBRARIES Qt6::Core Qt6::Test common_test_core
    TIMEOUT 30  LABELS unit config migration)

add_rd_test(TARGET test_settingsmanager_trustedhosts  NAME SettingsManagerTrustedHostsTest
    SOURCES common/config/test_settingsmanager_trustedhosts.cpp
    LIBRARIES Qt6::Core Qt6::Test common_test_core
    TIMEOUT 30  LABELS unit config trust)

# unit / capture
add_rd_test(TARGET test_screencaptureworker  NAME ScreenCaptureWorkerTest
    SOURCES server/capture/test_screencaptureworker.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Network Qt6::Concurrent capture_test_core
    TIMEOUT 30  LABELS unit capture worker)

add_rd_test(TARGET test_screencapture  NAME ScreenCaptureTest
    SOURCES server/capture/test_screencapture.cpp
    EXTRA_SOURCES ../src/server/capture/ScreenCapture.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Network Qt6::Concurrent capture_test_core
    TIMEOUT 60  LABELS unit capture main)

add_rd_test(TARGET test_dxgi_capture  NAME DxgiCaptureTest
    SOURCES server/capture/test_dxgi_capture.cpp
    EXTRA_SOURCES ../src/common/network/ProtocolImpl.cpp ../src/common/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui common_test_core
    TIMEOUT 30  LABELS unit capture dxgi windows)
if(WIN32)
    target_sources(test_dxgi_capture PRIVATE ../src/server/capture/windows/DxgiCapture.cpp)
    target_link_libraries(test_dxgi_capture PRIVATE d3d11 dxgi)
endif()

# unit / server
add_rd_test(TARGET test_queuemanager  NAME QueueManagerTest
    SOURCES server/dataflow/test_queuemanager.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::Network capture_test_core
    TIMEOUT 30  LABELS unit server queue)

# DataProcessing 测试已移除——原 DataProcessor/Validator/Cleaner 管线已删除
# DataProcessingTest 已移除——原 DataProcessor/Validator/Cleaner 类链已从源码删除

# unit / dataflow
add_rd_test(TARGET test_captured_frame  NAME CapturedFrameTest
    SOURCES server/dataflow/test_captured_frame.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui common_test_core
    TIMEOUT 30  LABELS unit server dataflow)

# unit / client
add_rd_test(TARGET test_refresh_latency_metric  NAME RefreshLatencyMetricTest
    SOURCES client/window/test_refresh_latency_metric.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui common_test_core
    TIMEOUT 30  LABELS unit client metrics)

add_rd_test(TARGET test_session_latest_wins  NAME SessionLatestWinsTest
    SOURCES client/session/test_session_latest_wins.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui
    TIMEOUT 30  LABELS unit client queue)

add_rd_test(TARGET test_triple_buffer_swap  NAME TripleBufferSwapTest
    SOURCES client/window/test_triple_buffer_swap.cpp
    LIBRARIES Qt6::Core Qt6::Test
    TIMEOUT 30  LABELS unit client threading)

# unit / render
add_rd_test(TARGET test_render_surface  NAME RenderSurfaceTest
    SOURCES client/decode/test_render_surface.cpp
    EXTRA_SOURCES ../src/client/window/GLTextureViewport.cpp
                  ../src/client/decode/GpuDecodeTarget.cpp
                  ../src/client/window/CursorManager.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets common_test_core
    TIMEOUT 30  LABELS unit client render)

# unit / client window
add_rd_test(TARGET test_clientremotewindow  NAME ClientRemoteWindowTest
    SOURCES client/window/test_clientremotewindow.cpp
    EXTRA_SOURCES
        ../src/client/window/ClientRemoteWindow.cpp
        ../src/client/window/InputForwarder.cpp
        ../src/client/window/ConnectionLifecycle.cpp
        ../src/client/window/CursorManager.cpp
        ../src/client/session/ProtocolSession.cpp
        ../src/client/session/DecodePipeline.cpp
        ../src/client/managers/DecodeWorker.cpp
        ../src/client/decode/TurboJpegDecoder.cpp
        ../src/client/decode/windows/NvJpegDecoder.cpp
        ../src/client/window/GLTextureViewport.cpp
        ../src/client/decode/GpuDecodeTarget.cpp

        ../src/client/network/ConnectionManager.cpp
        ../src/client/network/ServerTrustStore.cpp
        ../src/client/network/TcpClient.cpp
        ../src/common/network/ProtocolImpl.cpp
        ../src/common/network/MessageCodec.cpp
        ../src/common/clipboard/ClipboardManager.cpp
        ../src/client/window/FloatingRemoteToolbar.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::Network Qt6::OpenGL Qt6::OpenGLWidgets OpenSSL::SSL OpenSSL::Crypto common_test_core LibJpegTurbo::turbojpeg $<$<BOOL:${WIN32}>:Cuda::cudart> $<$<BOOL:${WIN32}>:Cuda::nvjpeg>
    TIMEOUT 30  LABELS unit client window)

# unit / server worker
add_rd_test(TARGET test_clienthandlerworker  NAME ClientHandlerWorkerTest
    SOURCES server/clienthandler/test_clienthandlerworker.cpp
    LIBRARIES Qt6::Core Qt6::Test common_test_core
    TIMEOUT 60  LABELS unit server worker network)

# unit / server auth
add_rd_test(TARGET test_authhandler  NAME AuthHandlerTest
    SOURCES server/clienthandler/test_authhandler.cpp
    EXTRA_SOURCES ../src/server/clienthandler/AuthHandler.cpp
    LIBRARIES Qt6::Core Qt6::Test common_test_core
    TIMEOUT 30  LABELS unit server auth)

# integration
add_rd_test(TARGET test_screen_capture_integration  NAME ScreenCaptureIntegrationTest
    SOURCES integration/test_screen_capture_integration.cpp
    EXTRA_SOURCES ../src/server/capture/ScreenCapture.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Network Qt6::Concurrent capture_test_core
    TIMEOUT 300  LABELS integration screencapture redundancy)

add_rd_test(TARGET test_data_consistency  NAME DataConsistencyTest
    SOURCES server/dataflow/test_data_consistency.cpp
    EXTRA_SOURCES ../src/common/network/ProtocolImpl.cpp ../src/common/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Network threading_test_core
    TIMEOUT 30  LABELS integration consistency network)

add_rd_test(TARGET test_screen_data_flow  NAME ScreenDataFlowTest
    SOURCES integration/test_screen_data_flow.cpp
    EXTRA_SOURCES ../src/common/network/ProtocolImpl.cpp ../src/common/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Concurrent OpenSSL::SSL OpenSSL::Crypto common_test_core
    TIMEOUT 120  LABELS integration screen data flow network)

add_rd_test(TARGET test_producer_consumer_integration  NAME ProducerConsumerIntegrationTest
    SOURCES integration/test_producer_consumer_integration.cpp
    EXTRA_SOURCES
        ../src/server/dataprocessing/DataProcessingWorker.cpp

        ../src/server/capture/ScreenCapture.cpp
        ../src/server/clienthandler/ClientHandlerWorker.cpp
        ../src/server/clienthandler/AuthHandler.cpp
        ../src/server/service/TcpServer.cpp
        ../src/server/simulator/InputSimulator.cpp
        ../src/server/simulator/MouseSimulator.cpp
        ../src/server/simulator/KeyboardSimulator.cpp
        ../src/common/network/ProtocolImpl.cpp
        ../src/common/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Network Qt6::Concurrent OpenSSL::SSL OpenSSL::Crypto capture_test_core LibJpegTurbo::turbojpeg
    TIMEOUT 300  LABELS integration producer consumer queue threading)
if(WIN32)
    target_sources(test_producer_consumer_integration PRIVATE
        ../src/server/simulator/windows/MouseSimulatorWindows.cpp
        ../src/server/simulator/windows/KeyboardSimulatorWindows.cpp)
elseif(APPLE)
    target_sources(test_producer_consumer_integration PRIVATE
        ../src/server/simulator/macos/MouseSimulatorMacOS.cpp
        ../src/server/simulator/macos/KeyboardSimulatorMacOS.cpp)
elseif(UNIX)
    target_sources(test_producer_consumer_integration PRIVATE
        ../src/server/simulator/linux/MouseSimulatorLinux.cpp
        ../src/server/simulator/linux/KeyboardSimulatorLinux.cpp)
endif()

# integration / trust（端到端 TOFU：真实 TLS 首连记录 + 重连 Trusted）
add_rd_test(TARGET test_tofu_handshake  NAME TofuHandshakeTest
    SOURCES integration/test_tofu_handshake.cpp
    EXTRA_SOURCES
        ../src/client/network/ConnectionManager.cpp
        ../src/client/network/TcpClient.cpp
        ../src/client/network/ServerTrustStore.cpp
        ../src/server/service/TcpServer.cpp
        ../src/common/network/ProtocolImpl.cpp
        ../src/common/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Network OpenSSL::SSL OpenSSL::Crypto common_test_core
    TIMEOUT 60  LABELS integration trust tls network)

# integration / UI（需额外环境变量）
add_rd_test(TARGET test_close_event  NAME CloseEventTest
    SOURCES app/test_close_event.cpp
    LIBRARIES Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test Qt6::Concurrent
    TIMEOUT 60  LABELS integration ui closeevent app
    EXTRA_ENV "AUTO_RUN=1" "RD_MAIN_APP_PATH=${CMAKE_SOURCE_DIR}/Debug/CrossRemoteDesktop.app/Contents/MacOS/CrossRemoteDesktop")

# performance
add_rd_test(TARGET test_frame_transmission_latency  NAME FrameTransmissionLatencyTest
    SOURCES integration/test_frame_transmission_latency.cpp
    EXTRA_SOURCES ../src/common/network/ProtocolImpl.cpp ../src/common/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Network threading_test_core
    TIMEOUT 30  LABELS performance latency network)

# unit / client session
add_rd_test(TARGET test_protocolsession  NAME ProtocolSessionTest
    SOURCES client/session/test_protocolsession.cpp
    EXTRA_SOURCES
        ../src/client/session/ProtocolSession.cpp
        ../src/client/session/DecodePipeline.cpp
        ../src/client/managers/DecodeWorker.cpp
        ../src/client/decode/TurboJpegDecoder.cpp
        ../src/client/decode/windows/NvJpegDecoder.cpp
        ../src/client/window/GLTextureViewport.cpp
        ../src/client/decode/GpuDecodeTarget.cpp
        ../src/client/window/CursorManager.cpp
        ../src/client/network/ConnectionManager.cpp
        ../src/client/network/ServerTrustStore.cpp
        ../src/client/network/TcpClient.cpp
        ../src/common/network/ProtocolImpl.cpp
        ../src/common/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::Network Qt6::OpenGL Qt6::OpenGLWidgets common_test_core OpenSSL::SSL OpenSSL::Crypto LibJpegTurbo::turbojpeg $<$<BOOL:${WIN32}>:Cuda::cudart> $<$<BOOL:${WIN32}>:Cuda::nvjpeg>
    TIMEOUT 30  LABELS unit client session)

# unit / nvJPEG decoder（仅 Windows — 依赖 CUDA SDK）
if(WIN32)
add_rd_test(TARGET test_nvjpegdecoder  NAME NvJpegDecoderTest
    SOURCES client/decode/test_nvjpegdecoder.cpp
    EXTRA_SOURCES
        ../src/client/decode/windows/NvJpegDecoder.cpp
        ../src/client/decode/TurboJpegDecoder.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui common_test_core Cuda::cudart Cuda::nvjpeg LibJpegTurbo::turbojpeg
    TIMEOUT 30  LABELS unit client decode)
endif()

# smoke / network components
add_rd_test(TARGET test_smoke_network  NAME SmokeNetworkTest
    SOURCES client/network/test_smoke_network.cpp
    EXTRA_SOURCES
        ../src/client/network/ConnectionManager.cpp
        ../src/client/network/ServerTrustStore.cpp
        ../src/client/network/TcpClient.cpp
        ../src/common/network/ProtocolImpl.cpp
        ../src/common/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Network common_test_core
    TIMEOUT 30  LABELS smoke network unit)

add_rd_test(TARGET test_decodepipeline  NAME DecodePipelineRestartTest
    SOURCES client/session/test_decodepipeline.cpp
    EXTRA_SOURCES
        ../src/client/session/DecodePipeline.cpp
        ../src/client/managers/DecodeWorker.cpp
        ../src/client/decode/TurboJpegDecoder.cpp
        ../src/client/decode/windows/NvJpegDecoder.cpp
        ../src/client/window/GLTextureViewport.cpp
        ../src/client/decode/GpuDecodeTarget.cpp
        ../src/client/window/CursorManager.cpp
        ../src/common/network/ProtocolImpl.cpp
        ../src/common/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets common_test_core LibJpegTurbo::turbojpeg $<$<BOOL:${WIN32}>:Cuda::cudart> $<$<BOOL:${WIN32}>:Cuda::nvjpeg>
    TIMEOUT 30  LABELS unit client decode)

add_rd_test(TARGET test_connectionlifecycle  NAME ConnectionLifecycleTest
    SOURCES client/window/test_connectionlifecycle.cpp
    EXTRA_SOURCES ../src/client/window/ConnectionLifecycle.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Widgets common_test_core
    TIMEOUT 30  LABELS unit client trust)

add_rd_test(TARGET test_tcpclient_hostname  NAME TcpClientHostnameTest
    SOURCES client/network/test_tcpclient_hostname.cpp
    EXTRA_SOURCES
        ../src/client/network/TcpClient.cpp
        ../src/server/service/TcpServer.cpp
        ../src/common/network/ProtocolImpl.cpp
        ../src/common/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Network OpenSSL::SSL OpenSSL::Crypto common_test_core
    TIMEOUT 30  LABELS unit client tls trust)

# unit / common clipboard
add_rd_test(TARGET test_clipboardmanager  NAME ClipboardManagerTest
    SOURCES common/clipboard/test_clipboardmanager.cpp
    EXTRA_SOURCES ../src/common/clipboard/ClipboardManager.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets common_test_core
    TIMEOUT 30  LABELS unit common clipboard)

# unit / common network codec
add_rd_test(TARGET test_messagecodec  NAME MessageCodecTest
    SOURCES common/network/test_messagecodec.cpp
    EXTRA_SOURCES ../src/common/network/ProtocolImpl.cpp ../src/common/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Network common_test_core
    TIMEOUT 30  LABELS unit common network codec)

# unit / common network version
add_rd_test(TARGET test_appversion  NAME AppVersionTest
    SOURCES common/network/test_appversion.cpp
    EXTRA_SOURCES ../src/common/network/AppVersion.cpp
    LIBRARIES Qt6::Core Qt6::Test common_test_core
    TIMEOUT 30  LABELS unit common network version)

# unit / client trust
add_rd_test(TARGET test_servertruststore  NAME ServerTrustStoreTest
    SOURCES client/network/test_servertruststore.cpp
    EXTRA_SOURCES ../src/client/network/ServerTrustStore.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Network common_test_core
    TIMEOUT 30  LABELS unit client trust)

# unit / server trust
add_rd_test(TARGET test_server_cert_persistence  NAME ServerCertPersistenceTest
    SOURCES server/service/test_server_cert_persistence.cpp
    EXTRA_SOURCES ../src/server/service/TcpServer.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Network OpenSSL::SSL OpenSSL::Crypto common_test_core
    TIMEOUT 30  LABELS unit server trust tls)

# unit / client trust（ConnectionManager TOFU 信任门）
add_rd_test(TARGET test_connectionmanager_trust  NAME ConnectionManagerTrustTest
    SOURCES client/network/test_connectionmanager_trust.cpp
    EXTRA_SOURCES
        ../src/client/network/ConnectionManager.cpp
        ../src/client/network/TcpClient.cpp
        ../src/client/network/ServerTrustStore.cpp
        ../src/common/network/ProtocolImpl.cpp
        ../src/common/network/MessageCodec.cpp
    LIBRARIES Qt6::Core Qt6::Test Qt6::Network common_test_core
    TIMEOUT 30  LABELS unit client trust network)
