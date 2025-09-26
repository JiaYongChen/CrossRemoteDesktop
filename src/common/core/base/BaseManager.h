#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include "../types/CommonTypes.h"

/**
 * @file BaseManager.h
 * @brief 基础管理器类
 * 
 * 提供所有管理器类的通用基础功能，包括状态管理、错误处理、
 * 线程安全和生命周期管理等。
 */

namespace QtRemoteDesktop {

/**
 * @brief 管理器状态枚举
 */
enum class ManagerState {
    Uninitialized = 0,    ///< 未初始化
    Initializing = 1,     ///< 初始化中
    Ready = 2,            ///< 就绪
    Running = 3,          ///< 运行中
    Paused = 4,           ///< 暂停
    Stopping = 5,         ///< 停止中
    Stopped = 6,          ///< 已停止
    Error = 7             ///< 错误状态
};

/**
 * @brief 基础管理器类
 * 
 * 所有管理器类的基类，提供通用的状态管理、错误处理和线程安全功能。
 * 子类需要实现具体的初始化、启动、停止和清理逻辑。
 */
class BaseManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ManagerState state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit BaseManager(QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    virtual ~BaseManager();

    // 状态管理
    /**
     * @brief 获取当前状态
     * @return 管理器状态
     */
    ManagerState state() const;
    
    /**
     * @brief 检查是否已初始化
     * @return 是否已初始化
     */
    bool isInitialized() const;
    
    /**
     * @brief 检查是否正在运行
     * @return 是否正在运行
     */
    bool isRunning() const;
    
    /**
     * @brief 检查是否已停止
     * @return 是否已停止
     */
    bool isStopped() const;
    
    /**
     * @brief 检查是否有错误
     * @return 是否有错误
     */
    bool hasError() const;

    // 错误管理
    /**
     * @brief 获取最后的错误信息
     * @return 错误信息
     */
    QString lastError() const;
    
    /**
     * @brief 清除错误状态
     */
    void clearError();

    // 生命周期管理
    /**
     * @brief 初始化管理器
     * @return 是否成功
     */
    virtual bool initialize();
    
    /**
     * @brief 启动管理器
     * @return 是否成功
     */
    virtual bool start();
    
    /**
     * @brief 停止管理器
     */
    virtual void stop();
    
    /**
     * @brief 暂停管理器
     */
    virtual void pause();
    
    /**
     * @brief 恢复管理器
     */
    virtual void resume();
    
    /**
     * @brief 重启管理器
     * @return 是否成功
     */
    virtual bool restart();

signals:
    /**
     * @brief 状态改变信号
     * @param newState 新状态
     * @param oldState 旧状态
     */
    void stateChanged(ManagerState newState, ManagerState oldState);
    
    /**
     * @brief 错误发生信号
     * @param error 错误信息
     */
    void errorOccurred(const QString &error);
    
    /**
     * @brief 初始化完成信号
     * @param success 是否成功
     */
    void initialized(bool success);
    
    /**
     * @brief 启动完成信号
     * @param success 是否成功
     */
    void started(bool success);
    
    /**
     * @brief 停止完成信号
     */
    void stopped();

protected:
    /**
     * @brief 设置状态（线程安全）
     * @param newState 新状态
     */
    void setState(ManagerState newState);
    
    /**
     * @brief 设置错误信息（线程安全）
     * @param error 错误信息
     */
    void setError(const QString &error);
    
    /**
     * @brief 子类实现的初始化逻辑
     * @return 是否成功
     */
    virtual bool doInitialize() = 0;
    
    /**
     * @brief 子类实现的启动逻辑
     * @return 是否成功
     */
    virtual bool doStart() = 0;
    
    /**
     * @brief 子类实现的停止逻辑
     */
    virtual void doStop() = 0;
    
    /**
     * @brief 子类实现的暂停逻辑
     */
    virtual void doPause() {}
    
    /**
     * @brief 子类实现的恢复逻辑
     */
    virtual void doResume() {}
    
    /**
     * @brief 子类实现的清理逻辑
     */
    virtual void doCleanup() {}

private:
    mutable QMutex m_mutex;           ///< 线程安全互斥锁
    ManagerState m_state;             ///< 当前状态
    QString m_lastError;              ///< 最后的错误信息
    QDateTime m_createTime;           ///< 创建时间
    QDateTime m_initTime;             ///< 初始化时间
    QDateTime m_startTime;            ///< 启动时间
};

} // namespace QtRemoteDesktop

Q_DECLARE_METATYPE(QtRemoteDesktop::ManagerState)