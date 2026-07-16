#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

/**
 * @brief 跨平台开机自启动管理器
 *
 * 封装 Windows/macOS/Linux 三平台的自启动注册、注销和查询。
 * 通过 DI 注入使用，不是单例。
 */
class AutoStartManager : public QObject
{
    Q_OBJECT

public:
    explicit AutoStartManager(QObject *parent = nullptr);

    /// 注册或取消开机自启动（OS 层面）
    /// @return true 操作成功，false 失败（可通过 lastError() 获取原因）
    [[nodiscard]] bool setAutoStart(bool enable);

    /// 查询当前 OS 是否已注册开机自启动
    /// 通过比较注册路径与当前可执行文件路径来验证有效性
    bool isAutoStartEnabled() const;

    /// 最后一次 setAutoStart() 失败时的错误描述
    QString lastError() const;

private:
    /// 当前应用程序的完整路径（供平台实现使用）
    QString applicationPath() const;
    /// 注册项名称（平台无关标识符）
    QString applicationName() const;

    QString m_lastError;
};
