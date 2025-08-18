#ifndef PROTOCOLCODEC_H
#define PROTOCOLCODEC_H

#include "icodec.h"

// 基于现有 Protocol 的默认实现
class ProtocolCodec : public IMessageCodec {
public:
	QByteArray encode(MessageType type, const QByteArray &payload) override;
	bool tryDecode(QByteArray &buffer, MessageHeader &header, QByteArray &payload) override;
};

#endif // PROTOCOLCODEC_H
