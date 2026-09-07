# SetupVaApi.cmake — VA-API GPU JPEG 解码（Linux，尚未启用）
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

# 当前 VaApiDecoder 尚缺 JPEG 参数解析、有效 VAContext 创建以及 YUV 到 RGB
# 转换。在完整实现和测试落地前，不因系统恰好安装 libva 开发包而改变构建或
# 运行时路径；Linux 始终使用经过验证的 libjpeg-turbo 解码。
message(STATUS "VA-API JPEG 解码尚未启用，Linux 使用 libjpeg-turbo")
