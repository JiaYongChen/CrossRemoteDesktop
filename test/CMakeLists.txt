cmake_minimum_required(VERSION 3.16)
project(QtRemoteDesktopTests)
enable_testing()

# C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 和 Zlib
find_package(Qt6 REQUIRED COMPONENTS Core Test)
find_package(ZLIB REQUIRED)

set(CMAKE_AUTOMOC ON)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src/common/core)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src/common/codec)

# Compression diff/apply roundtrip tests
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_compression_diff.cpp")
    add_executable(test_compression_diff
        test_compression_diff.cpp
        ../src/common/core/compression.cpp
        ../src/common/core/constants.cpp
        ../src/common/core/logging_categories.cpp
        ../src/common/core/logger.cpp
        ../src/common/codec/compressor_factory.cpp
        ../src/common/codec/zlib_compressor.cpp
        ../src/common/codec/lz4_compressor.cpp
        ../src/common/codec/zstd_compressor.cpp
    )
    target_link_libraries(test_compression_diff Qt6::Core Qt6::Test Qt6::Gui ZLIB::ZLIB)
    target_include_directories(test_compression_diff PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common/codec
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common/core)

    if(ENABLE_LZ4)
        find_library(LZ4_LIBRARY NAMES lz4 liblz4 PATHS /usr/local/lib /opt/homebrew/lib)
        find_path(LZ4_INCLUDE_DIR NAMES lz4.h PATHS /usr/local/include /opt/homebrew/include)
        if(LZ4_LIBRARY AND LZ4_INCLUDE_DIR)
            target_link_libraries(test_compression_diff ${LZ4_LIBRARY})
            target_include_directories(test_compression_diff PRIVATE ${LZ4_INCLUDE_DIR})
            target_compile_definitions(test_compression_diff PRIVATE HAVE_LZ4)
        endif()
    endif()

    if(ENABLE_ZSTD)
        find_library(ZSTD_LIBRARY NAMES zstd libzstd PATHS /usr/local/lib /opt/homebrew/lib)
        find_path(ZSTD_INCLUDE_DIR NAMES zstd.h PATHS /usr/local/include /opt/homebrew/include)
        if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
            target_link_libraries(test_compression_diff ${ZSTD_LIBRARY})
            target_include_directories(test_compression_diff PRIVATE ${ZSTD_INCLUDE_DIR})
            target_compile_definitions(test_compression_diff PRIVATE HAVE_ZSTD)
        endif()
    endif()

    add_test(NAME test_compression_diff COMMAND test_compression_diff)
endif()

# Logger observability tests
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_logger.cpp")
    add_executable(test_logger
        test_logger.cpp
        ../src/common/core/logger.cpp
        ../src/common/core/logging_categories.cpp
        ../src/common/core/constants.cpp
    )
    target_link_libraries(test_logger Qt6::Core Qt6::Test)
    add_test(NAME test_logger COMMAND test_logger)
endif()

# ConnectionManager state machine tests
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_connection_manager.cpp")
    add_executable(test_connection_manager
        test_connection_manager.cpp
        ../src/client/managers/connectionmanager.cpp
        ../src/client/tcpclient.cpp
        ../src/common/core/encryption.cpp
        ../src/common/core/compression.cpp
        ../src/common/codec/compressor_factory.cpp
        ../src/common/codec/zlib_compressor.cpp
        ../src/common/codec/lz4_compressor.cpp
        ../src/common/codec/zstd_compressor.cpp
        ../src/common/core/protocol.cpp
        ../src/common/core/protocol_impl.cpp
        ../src/common/core/logging_categories.cpp
        ../src/common/core/logger.cpp
        ../src/common/core/constants.cpp
    )
    target_link_libraries(test_connection_manager Qt6::Core Qt6::Test ZLIB::ZLIB Qt6::Network Qt6::Gui OpenSSL::Crypto OpenSSL::SSL)
    add_test(NAME test_connection_manager COMMAND test_connection_manager)
    # Optional LZ4/ZSTD for parity with main build
    if(ENABLE_LZ4)
        find_library(LZ4_LIBRARY NAMES lz4 liblz4 PATHS /usr/local/lib /opt/homebrew/lib)
        find_path(LZ4_INCLUDE_DIR NAMES lz4.h PATHS /usr/local/include /opt/homebrew/include)
        if(LZ4_LIBRARY AND LZ4_INCLUDE_DIR)
            target_link_libraries(test_connection_manager ${LZ4_LIBRARY})
            target_include_directories(test_connection_manager PRIVATE ${LZ4_INCLUDE_DIR})
            target_compile_definitions(test_connection_manager PRIVATE HAVE_LZ4)
        endif()
    endif()
    if(ENABLE_ZSTD)
        find_library(ZSTD_LIBRARY NAMES zstd libzstd PATHS /usr/local/lib /opt/homebrew/lib)
        find_path(ZSTD_INCLUDE_DIR NAMES zstd.h PATHS /usr/local/include /opt/homebrew/include)
        if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
            target_link_libraries(test_connection_manager ${ZSTD_LIBRARY})
            target_include_directories(test_connection_manager PRIVATE ${ZSTD_INCLUDE_DIR})
            target_compile_definitions(test_connection_manager PRIVATE HAVE_ZSTD)
        endif()
    endif()
endif()

# SessionManager state machine and rendering tests
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_session_manager.cpp")
    add_executable(test_session_manager
        test_session_manager.cpp
        ../src/client/managers/sessionmanager.cpp
        ../src/client/managers/connectionmanager.cpp
        ../src/client/tcpclient.cpp
        ../src/common/core/encryption.cpp
        ../src/common/core/compression.cpp
        ../src/common/codec/compressor_factory.cpp
        ../src/common/codec/zlib_compressor.cpp
        ../src/common/codec/lz4_compressor.cpp
        ../src/common/codec/zstd_compressor.cpp
        ../src/common/core/protocol.cpp
        ../src/common/core/protocol_impl.cpp
        ../src/common/core/logging_categories.cpp
        ../src/common/core/logger.cpp
        ../src/common/core/constants.cpp
    )
    target_link_libraries(test_session_manager Qt6::Core Qt6::Test Qt6::Gui Qt6::Widgets Qt6::Network ZLIB::ZLIB OpenSSL::Crypto OpenSSL::SSL)
    add_test(NAME test_session_manager COMMAND test_session_manager)
    if(ENABLE_LZ4)
        find_library(LZ4_LIBRARY NAMES lz4 liblz4 PATHS /usr/local/lib /opt/homebrew/lib)
        find_path(LZ4_INCLUDE_DIR NAMES lz4.h PATHS /usr/local/include /opt/homebrew/include)
        if(LZ4_LIBRARY AND LZ4_INCLUDE_DIR)
            target_link_libraries(test_session_manager ${LZ4_LIBRARY})
            target_include_directories(test_session_manager PRIVATE ${LZ4_INCLUDE_DIR})
            target_compile_definitions(test_session_manager PRIVATE HAVE_LZ4)
        endif()
    endif()
    if(ENABLE_ZSTD)
        find_library(ZSTD_LIBRARY NAMES zstd libzstd PATHS /usr/local/lib /opt/homebrew/lib)
        find_path(ZSTD_INCLUDE_DIR NAMES zstd.h PATHS /usr/local/include /opt/homebrew/include)
        if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
            target_link_libraries(test_session_manager ${ZSTD_LIBRARY})
            target_include_directories(test_session_manager PRIVATE ${ZSTD_INCLUDE_DIR})
            target_compile_definitions(test_session_manager PRIVATE HAVE_ZSTD)
        endif()
    endif()
endif()

# 压缩基准测试（使用 Compression::benchmarkAllAlgorithms）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_compression_bench.cpp")
    add_executable(test_compression_bench
        test_compression_bench.cpp
        ../src/common/core/compression.cpp
        ../src/common/core/constants.cpp
        ../src/common/core/logging_categories.cpp
        ../src/common/core/logger.cpp
        ../src/common/codec/compressor_factory.cpp
        ../src/common/codec/zlib_compressor.cpp
        ../src/common/codec/lz4_compressor.cpp
        ../src/common/codec/zstd_compressor.cpp
    )
    target_link_libraries(test_compression_bench Qt6::Core Qt6::Test Qt6::Gui ZLIB::ZLIB)
    target_include_directories(test_compression_bench PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common/codec
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common/core)

    if(ENABLE_LZ4)
        find_library(LZ4_LIBRARY NAMES lz4 liblz4 PATHS /usr/local/lib /opt/homebrew/lib)
        find_path(LZ4_INCLUDE_DIR NAMES lz4.h PATHS /usr/local/include /opt/homebrew/include)
        if(LZ4_LIBRARY AND LZ4_INCLUDE_DIR)
            target_link_libraries(test_compression_bench ${LZ4_LIBRARY})
            target_include_directories(test_compression_bench PRIVATE ${LZ4_INCLUDE_DIR})
            target_compile_definitions(test_compression_bench PRIVATE HAVE_LZ4)
        endif()
    endif()

    if(ENABLE_ZSTD)
        find_library(ZSTD_LIBRARY NAMES zstd libzstd PATHS /usr/local/lib /opt/homebrew/lib)
        find_path(ZSTD_INCLUDE_DIR NAMES zstd.h PATHS /usr/local/include /opt/homebrew/include)
        if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
            target_link_libraries(test_compression_bench ${ZSTD_LIBRARY})
            target_include_directories(test_compression_bench PRIVATE ${ZSTD_INCLUDE_DIR})
            target_compile_definitions(test_compression_bench PRIVATE HAVE_ZSTD)
        endif()
    endif()

    add_test(NAME test_compression_bench COMMAND test_compression_bench)
endif()
# 压缩功能与宏开关测试/基准
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_compression_features.cpp")
    add_executable(test_compression_features
        test_compression_features.cpp
        ../src/common/codec/zlib_compressor.cpp
        ../src/common/codec/lz4_compressor.cpp
        ../src/common/codec/zstd_compressor.cpp
    )
    target_link_libraries(test_compression_features Qt6::Core Qt6::Test ZLIB::ZLIB)

    # 与主工程保持一致的宏和包含路径（可选）
    target_include_directories(test_compression_features PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/common/codec)

    # 如果主工程启用了对应库，尝试链接（在macOS/Homebrew路径上优先）
    if(ENABLE_LZ4)
        find_library(LZ4_LIBRARY NAMES lz4 liblz4 PATHS /usr/local/lib /opt/homebrew/lib)
        find_path(LZ4_INCLUDE_DIR NAMES lz4.h PATHS /usr/local/include /opt/homebrew/include)
        if(LZ4_LIBRARY AND LZ4_INCLUDE_DIR)
            target_link_libraries(test_compression_features ${LZ4_LIBRARY})
            target_include_directories(test_compression_features PRIVATE ${LZ4_INCLUDE_DIR})
            target_compile_definitions(test_compression_features PRIVATE HAVE_LZ4)
        endif()
    endif()

    if(ENABLE_ZSTD)
        find_library(ZSTD_LIBRARY NAMES zstd libzstd PATHS /usr/local/lib /opt/homebrew/lib)
        find_path(ZSTD_INCLUDE_DIR NAMES zstd.h PATHS /usr/local/include /opt/homebrew/include)
        if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
            target_link_libraries(test_compression_features ${ZSTD_LIBRARY})
            target_include_directories(test_compression_features PRIVATE ${ZSTD_INCLUDE_DIR})
            target_compile_definitions(test_compression_features PRIVATE HAVE_ZSTD)
        endif()
    endif()

    add_test(NAME test_compression_features COMMAND test_compression_features)
else()
    message(STATUS "Skipping test_compression_features: not found")
endif()