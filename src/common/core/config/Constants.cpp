#include "Constants.h"
#include <QtCore/QDateTime>
#include <QtCore/QCoreApplication>
#include <QtCore/QThread>

// 版本信息静态成员定义
const QString CoreConstants::Version::VERSION_STRING = 
    QString("%1.%2.%3").arg(MAJOR).arg(MINOR).arg(PATCH);
const QString CoreConstants::Version::BUILD_DATE = 
    QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

// 安全相关字符串常量
const QString CoreConstants::Security::DEFAULT_CIPHER_SUITE = "AES256-GCM-SHA384";

/**
 * @brief 获取应用程序版本信息
 * @return 版本字符串
 */
QString CoreConstants::getVersionString()
{
    return Version::VERSION_STRING;
}

/**
 * @brief 获取构建日期
 * @return 构建日期字符串
 */
QString CoreConstants::getBuildDate()
{
    return Version::BUILD_DATE;
}

/**
 * @brief 验证帧率是否在有效范围内
 * @param fps 帧率值
 * @return 是否有效
 */
bool CoreConstants::isValidFrameRate(int fps)
{
    return fps >= Capture::MIN_FRAME_RATE && fps <= Capture::MAX_FRAME_RATE;
}

/**
 * @brief 验证端口号是否有效
 * @param port 端口号
 * @return 是否有效
 */
bool CoreConstants::isValidPort(int port)
{
    return port > 0 && port <= 65535;
}

/**
 * @brief 获取推荐的线程池大小
 * @return 线程池大小
 */
int CoreConstants::getRecommendedThreadPoolSize()
{
    int coreCount = QThread::idealThreadCount();
    return qMax(Performance::THREAD_POOL_SIZE, coreCount);
}
