cmake_minimum_required(VERSION 3.16)

project(QtRemoteDesktop VERSION 1.0.0 LANGUAGES CXX)
# Feature options
option(ENABLE_LZ4 "Enable optional LZ4 compression" ON)
option(ENABLE_ZSTD "Enable optional Zstandard compression" ON)


# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找Qt6组件
find_package(Qt6 6.9 REQUIRED COMPONENTS
    Core
    Widgets
    Network
    Multimedia
    Gui
    OpenGL
    OpenGLWidgets
)

# 启用Qt的MOC、UIC和RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 设置UIC搜索路径
set(CMAKE_AUTOUIC_SEARCH_PATHS src/ui)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 包含目录
include_directories(
    src
    src/common
    src/common/core
    src/common/windows
    src/client/managers
    src/server
    src/server/capture
    src/client
)

# 源文件
set(SOURCES
    src/main.cpp
    src/common/windows/mainwindow.cpp
    src/server/tcpserver.cpp
    src/server/clienthandler.cpp
    src/client/tcpclient.cpp
    src/common/core/protocol.cpp
    src/server/screencapture.cpp
    src/client/inputhandler.cpp
    src/server/inputsimulator.cpp
    src/common/windows/connectiondialog.cpp
    src/client/clientremotewindow.cpp
    src/common/windows/settingsdialog.cpp
    src/common/core/encryption.cpp
    src/common/core/compression.cpp
    src/common/core/logger.cpp
    src/common/core/config.cpp
    src/common/core/constants.cpp
    src/server/servermanager.cpp
    src/client/clientmanager.cpp

    src/client/managers/connectionmanager.cpp
    src/client/managers/sessionmanager.cpp

)

# 头文件
set(HEADERS
    src/common/windows/mainwindow.h
    src/server/tcpserver.h
    src/server/clienthandler.h
    src/client/tcpclient.h
    src/common/core/protocol.h
    src/server/screencapture.h
    src/client/inputhandler.h
    src/server/inputsimulator.h
    src/common/windows/connectiondialog.h
    src/client/clientremotewindow.h
    src/common/windows/settingsdialog.h
    src/common/core/encryption.h
    src/common/core/compression.h
    src/common/core/logger.h
    src/common/core/config.h
    src/server/servermanager.h
    src/client/clientmanager.h

    src/client/managers/connectionmanager.h
    src/client/managers/sessionmanager.h

)

# UI文件
set(UI_FILES
    src/ui/mainwindow.ui
    src/ui/connectiondialog.ui
    src/ui/settingsdialog.ui
)

# 资源文件
set(RESOURCES
    resources/resources.qrc
)

# 创建可执行文件
add_executable(QtRemoteDesktop
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
    ${RESOURCES}
)

# 链接Qt库
target_link_libraries(QtRemoteDesktop
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::Gui
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)

# 平台特定设置
if(WIN32)
    # Windows特定设置
    target_link_libraries(QtRemoteDesktop
        ws2_32
        user32
        gdi32
        ole32
        oleaut32
        uuid
        advapi32
        shell32
    )
    
    # 设置Windows应用程序图标
    set_target_properties(QtRemoteDesktop PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
elseif(APPLE)
    # macOS特定设置
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    find_library(COREGRAPHICS_LIBRARY CoreGraphics)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(COCOA_LIBRARY Cocoa)
    
    target_link_libraries(QtRemoteDesktop
        ${COREFOUNDATION_LIBRARY}
        ${COREGRAPHICS_LIBRARY}
        ${IOKIT_LIBRARY}
        ${COCOA_LIBRARY}
    )
    
    # 设置macOS应用程序包
    set_target_properties(QtRemoteDesktop PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/Info.plist.in
    )
endif()

# 查找并链接OpenSSL
find_package(OpenSSL REQUIRED)
target_link_libraries(QtRemoteDesktop OpenSSL::SSL OpenSSL::Crypto)

# 查找并链接zlib
find_package(ZLIB REQUIRED)
target_link_libraries(QtRemoteDesktop ZLIB::ZLIB)

if(ENABLE_ZSTD)
    # Find and link ZSTD library
    find_library(ZSTD_LIBRARY NAMES zstd libzstd PATHS /usr/local/lib /opt/homebrew/lib)
    find_path(ZSTD_INCLUDE_DIR NAMES zstd.h PATHS /usr/local/include /opt/homebrew/include)
    if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
        target_link_libraries(QtRemoteDesktop ${ZSTD_LIBRARY})
        target_include_directories(QtRemoteDesktop PRIVATE ${ZSTD_INCLUDE_DIR})
        target_compile_definitions(QtRemoteDesktop PRIVATE HAVE_ZSTD)
        message(STATUS "Found ZSTD: ${ZSTD_LIBRARY}")
    else()
        message(WARNING "ZSTD library not found; ZSTD disabled")
    endif()
else()
    message(STATUS "ZSTD disabled by option")
endif()

if(ENABLE_LZ4)
    # Find and link LZ4 library
    find_library(LZ4_LIBRARY NAMES lz4 liblz4 PATHS /usr/local/lib /opt/homebrew/lib)
    find_path(LZ4_INCLUDE_DIR NAMES lz4.h PATHS /usr/local/include /opt/homebrew/include)
    if(LZ4_LIBRARY AND LZ4_INCLUDE_DIR)
        target_link_libraries(QtRemoteDesktop ${LZ4_LIBRARY})
        target_include_directories(QtRemoteDesktop PRIVATE ${LZ4_INCLUDE_DIR})
        target_compile_definitions(QtRemoteDesktop PRIVATE HAVE_LZ4)
        message(STATUS "Found LZ4: ${LZ4_LIBRARY}")
    else()
        message(WARNING "LZ4 library not found; LZ4 disabled")
    endif()
else()
    message(STATUS "LZ4 disabled by option")
endif()



# 平台特定的库链接（用于InputSimulator）
if(APPLE)
    # macOS需要ApplicationServices框架
    find_library(APPLICATION_SERVICES_FRAMEWORK ApplicationServices)
    if(APPLICATION_SERVICES_FRAMEWORK)
        target_link_libraries(QtRemoteDesktop ${APPLICATION_SERVICES_FRAMEWORK})
        message(STATUS "Found ApplicationServices framework")
    else()
        message(WARNING "ApplicationServices framework not found")
    endif()
elseif(UNIX AND NOT APPLE)
    # Linux需要X11库
    find_package(X11 REQUIRED)
    if(X11_FOUND)
        target_link_libraries(QtRemoteDesktop ${X11_LIBRARIES})
        target_include_directories(QtRemoteDesktop PRIVATE ${X11_INCLUDE_DIR})
        
        # 查找XTest扩展
        find_library(XTEST_LIBRARY Xtst PATHS ${X11_LIB_SEARCH_PATH})
        if(XTEST_LIBRARY)
            target_link_libraries(QtRemoteDesktop ${XTEST_LIBRARY})
            message(STATUS "Found XTest library: ${XTEST_LIBRARY}")
        else()
            message(WARNING "XTest library not found")
        endif()
        
        message(STATUS "Found X11 libraries")
    else()
        message(FATAL_ERROR "X11 libraries not found")
    endif()
elseif(WIN32)
    # Windows需要user32库（通常自动链接）
    target_link_libraries(QtRemoteDesktop user32)
    message(STATUS "Linked Windows user32 library")
endif()

# 编译器特定设置
if(MSVC)
    target_compile_options(QtRemoteDesktop PRIVATE /W4)
else()
    target_compile_options(QtRemoteDesktop PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 调试信息
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(QtRemoteDesktop PRIVATE DEBUG_MODE)
endif()

# 安装规则
install(TARGETS QtRemoteDesktop
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# Qt部署
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET QtRemoteDesktop POST_BUILD
            COMMAND ${WINDEPLOYQT_EXECUTABLE} $<TARGET_FILE:QtRemoteDesktop>
            COMMENT "Deploying Qt libraries")
    endif()
elseif(APPLE)
    # 设置正确的rpath，不使用macdeployqt避免错误信息
    set_target_properties(QtRemoteDesktop PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks;@loader_path/../Frameworks;/usr/local/opt/qt/lib"
        BUILD_WITH_INSTALL_RPATH TRUE
        MACOSX_RPATH TRUE
        SKIP_BUILD_RPATH FALSE
        BUILD_RPATH_USE_ORIGIN TRUE
    )
    
    # 手动复制必要的Qt框架到应用程序包
    add_custom_command(TARGET QtRemoteDesktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_DIR:QtRemoteDesktop>/Contents/Frameworks
        COMMAND ${CMAKE_COMMAND} -E copy_directory /usr/local/opt/qt/lib/QtCore.framework $<TARGET_BUNDLE_DIR:QtRemoteDesktop>/Contents/Frameworks/QtCore.framework
        COMMAND ${CMAKE_COMMAND} -E copy_directory /usr/local/opt/qt/lib/QtGui.framework $<TARGET_BUNDLE_DIR:QtRemoteDesktop>/Contents/Frameworks/QtGui.framework
        COMMAND ${CMAKE_COMMAND} -E copy_directory /usr/local/opt/qt/lib/QtWidgets.framework $<TARGET_BUNDLE_DIR:QtRemoteDesktop>/Contents/Frameworks/QtWidgets.framework
        COMMAND ${CMAKE_COMMAND} -E copy_directory /usr/local/opt/qt/lib/QtNetwork.framework $<TARGET_BUNDLE_DIR:QtRemoteDesktop>/Contents/Frameworks/QtNetwork.framework
        COMMAND ${CMAKE_COMMAND} -E copy_directory /usr/local/opt/qt/lib/QtMultimedia.framework $<TARGET_BUNDLE_DIR:QtRemoteDesktop>/Contents/Frameworks/QtMultimedia.framework
        COMMAND ${CMAKE_COMMAND} -E copy_directory /usr/local/opt/qt/lib/QtOpenGL.framework $<TARGET_BUNDLE_DIR:QtRemoteDesktop>/Contents/Frameworks/QtOpenGL.framework
        COMMENT "Manually deploying Qt libraries")
    
    # 修复框架内的install_name
    add_custom_command(TARGET QtRemoteDesktop POST_BUILD
        COMMAND install_name_tool -change @rpath/QtCore.framework/Versions/A/QtCore @executable_path/../Frameworks/QtCore.framework/Versions/A/QtCore $<TARGET_FILE:QtRemoteDesktop>
        COMMAND install_name_tool -change @rpath/QtGui.framework/Versions/A/QtGui @executable_path/../Frameworks/QtGui.framework/Versions/A/QtGui $<TARGET_FILE:QtRemoteDesktop>
        COMMAND install_name_tool -change @rpath/QtWidgets.framework/Versions/A/QtWidgets @executable_path/../Frameworks/QtWidgets.framework/Versions/A/QtWidgets $<TARGET_FILE:QtRemoteDesktop>
        COMMAND install_name_tool -change @rpath/QtNetwork.framework/Versions/A/QtNetwork @executable_path/../Frameworks/QtNetwork.framework/Versions/A/QtNetwork $<TARGET_FILE:QtRemoteDesktop>
        COMMAND install_name_tool -change @rpath/QtMultimedia.framework/Versions/A/QtMultimedia @executable_path/../Frameworks/QtMultimedia.framework/Versions/A/QtMultimedia $<TARGET_FILE:QtRemoteDesktop>
        COMMAND install_name_tool -change @rpath/QtOpenGL.framework/Versions/A/QtOpenGL @executable_path/../Frameworks/QtOpenGL.framework/Versions/A/QtOpenGL $<TARGET_FILE:QtRemoteDesktop>
        COMMENT "Fixing framework install names")
    
    # 可选：如果用户想要使用macdeployqt，可以取消注释以下行
    # find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS /usr/local/opt/qt/bin)
    # if(MACDEPLOYQT_EXECUTABLE)
    #     add_custom_command(TARGET QtRemoteDesktop POST_BUILD
    #         COMMAND ${MACDEPLOYQT_EXECUTABLE} $<TARGET_BUNDLE_DIR:QtRemoteDesktop> -libpath=/usr/local/opt/qt/lib -always-overwrite
    #         COMMENT "Deploying Qt libraries with macdeployqt")
    # endif()
endif()

# 启用测试
enable_testing()

# 添加测试子目录
add_subdirectory(test)

# 打印配置信息
message(STATUS "Qt version: ${Qt6_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")