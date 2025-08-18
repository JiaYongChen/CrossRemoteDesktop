#include "protocolcodec.h"
#include "protocol.h"
#include <QtCore/QDataStream>

QByteArray ProtocolCodec::encode(MessageType type, const QByteArray &payload)
{
	return Protocol::createMessage(type, payload);
}

bool ProtocolCodec::tryDecode(QByteArray &buffer, MessageHeader &header, QByteArray &payload)
{
	if (buffer.size() < static_cast<qsizetype>(SERIALIZED_HEADER_SIZE)) {
		return false;
	}

	// 快速检查魔数并做一次轻量级重同步
	QByteArray headerData = buffer.left(static_cast<int>(SERIALIZED_HEADER_SIZE));
	QDataStream stream(headerData);
	stream.setByteOrder(QDataStream::LittleEndian);
	quint32 magic = 0;
	stream >> magic;
	if (magic != PROTOCOL_MAGIC) {
		QByteArray magicBytes;
		QDataStream ms(&magicBytes, QIODevice::WriteOnly);
		ms.setByteOrder(QDataStream::LittleEndian);
		ms << PROTOCOL_MAGIC;
		int nextMagicPos = buffer.indexOf(magicBytes, 1);
		if (nextMagicPos > 0) {
			buffer.remove(0, nextMagicPos); // 丢弃无效前缀
		} else {
			buffer.remove(0, 1);
		}
		return false; // 等待更多数据再解析
	}

	// 使用现有协议解析，但不直接传 buffer（以免误消费），而是传入一份视图
	QByteArray view = buffer; // 复制以解析
	if (!Protocol::parseMessage(view, header, payload)) {
		return false;
	}

	// 成功则移除已消费字节
	const qsizetype total = static_cast<qsizetype>(SERIALIZED_HEADER_SIZE) + header.length;
	if (buffer.size() >= total) {
		buffer.remove(0, total);
	}
	return true;
}

