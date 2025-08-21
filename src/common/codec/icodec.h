#ifndef ICODEC_H
#define ICODEC_H

#include <QtCore/QByteArray>

// 编解码接口：仅负责消息打包与从缓冲区解包
class IMessageCodec {
public:
	virtual ~IMessageCodec() = default;

	// 将类型与载荷编码为可发送的数据帧
	virtual QByteArray encode() const = 0;

	// 从接收缓冲区尝试解析一帧，成功则填充header与payload，并从buffer移除已消费字节
	virtual bool decode(const QByteArray &dataBuffer) = 0;
};

#endif // ICODEC_H
