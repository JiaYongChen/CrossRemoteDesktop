#pragma once

#include "ErrorCode.h"
#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>

/// 统一错误类型，替代项目中所有 QString-based 错误信号
struct RdError {
    ErrorCode code = ErrorCode::Unknown;
    QString message;
    QString source;     // 类名/模块名
    qint64 timestampMs = 0;

    RdError() = default;
    RdError(ErrorCode c, QString msg, QString src = {})
        : code(c), message(std::move(msg)), source(std::move(src))
        , timestampMs(QDateTime::currentMSecsSinceEpoch()) {}

    [[nodiscard]] QString logLabel() const {
        return source.isEmpty() ? message
             : QStringLiteral("%1: %2").arg(source, message);
    }

    friend QDebug operator<<(QDebug dbg, const RdError& err) {
        QDebugStateSaver saver(dbg);
        dbg.nospace() << err.logLabel();
        return dbg;
    }
};

Q_DECLARE_METATYPE(RdError)
