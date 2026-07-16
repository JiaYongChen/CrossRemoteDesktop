#include "AutoStartManager.h"
#include "common/logging/LoggingCategories.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#elif defined(Q_OS_MACOS)
#include <QtCore/QXmlStreamReader>
#include <QtCore/QXmlStreamWriter>
#elif defined(Q_OS_LINUX)
#include <QtCore/QTextStream>
#endif

// ============================================================
// 构造 / 基本方法
// ============================================================

AutoStartManager::AutoStartManager(QObject *parent)
    : QObject(parent)
{
}

QString AutoStartManager::applicationPath() const
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

QString AutoStartManager::applicationName() const
{
    return QStringLiteral("CrossRemoteDesktop");
}

QString AutoStartManager::lastError() const
{
    return m_lastError;
}

// ============================================================
// Windows 实现 — 注册表 HKCU\Software\Microsoft\Windows\CurrentVersion\Run
// ============================================================
#ifdef Q_OS_WIN

bool AutoStartManager::setAutoStart(bool enable)
{
    const QString appPath = applicationPath();
    const QString appName = applicationName();

    if (enable) {
        // 打开或创建 Run 键
        HKEY hKey = nullptr;
        LONG result = RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &hKey);
        if (result != ERROR_SUCCESS) {
            m_lastError = QStringLiteral("无法打开注册表 Run 键 (错误码: %1)")
                .arg(result);
            qCWarning(lcCoreConfig) << "AutoStartManager:" << m_lastError;
            return false;
        }

        // 写入值（路径带引号防止空格路径问题）
        const QString quotedPath = QStringLiteral("\"%1\"").arg(appPath);
        result = RegSetValueExW(hKey,
            reinterpret_cast<LPCWSTR>(appName.utf16()),
            0, REG_SZ,
            reinterpret_cast<const BYTE*>(quotedPath.utf16()),
            static_cast<DWORD>((quotedPath.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);

        if (result != ERROR_SUCCESS) {
            m_lastError = QStringLiteral("无法写入注册表 Run 键 (错误码: %1)")
                .arg(result);
            qCWarning(lcCoreConfig) << "AutoStartManager:" << m_lastError;
            return false;
        }

        qCInfo(lcCoreConfig) << "AutoStartManager: 已注册开机自启动";
        return true;
    } else {
        // 删除键值
        HKEY hKey = nullptr;
        LONG result = RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &hKey);
        if (result == ERROR_FILE_NOT_FOUND) {
            // 键不存在 = 已在注销状态，视为成功
            return true;
        }
        if (result != ERROR_SUCCESS) {
            m_lastError = QStringLiteral("无法打开注册表 Run 键进行删除 (错误码: %1)")
                .arg(result);
            qCWarning(lcCoreConfig) << "AutoStartManager:" << m_lastError;
            return false;
        }

        result = RegDeleteValueW(hKey,
            reinterpret_cast<LPCWSTR>(appName.utf16()));
        RegCloseKey(hKey);

        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
            m_lastError = QStringLiteral("无法删除注册表 Run 键值 (错误码: %1)")
                .arg(result);
            qCWarning(lcCoreConfig) << "AutoStartManager:" << m_lastError;
            return false;
        }

        qCInfo(lcCoreConfig) << "AutoStartManager: 已取消开机自启动";
        return true;
    }
}

bool AutoStartManager::isAutoStartEnabled() const
{
    const QString appName = applicationName();
    const QString currentPath = applicationPath();

    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    wchar_t buffer[2048] = {};
    DWORD bufferSize = sizeof(buffer);
    DWORD type = 0;

    result = RegQueryValueExW(hKey,
        reinterpret_cast<LPCWSTR>(appName.utf16()),
        nullptr, &type,
        reinterpret_cast<LPBYTE>(buffer), &bufferSize);
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS || type != REG_SZ) {
        return false;
    }

    // 比较路径（忽略引号包裹差异）
    QString registeredPath = QString::fromWCharArray(buffer).trimmed();
    if (registeredPath.startsWith('\"') && registeredPath.endsWith('\"')) {
        registeredPath = registeredPath.mid(1, registeredPath.size() - 2);
    }
    registeredPath = QDir::toNativeSeparators(registeredPath);

    return QString::compare(registeredPath, currentPath, Qt::CaseInsensitive) == 0;
}

// ============================================================
// macOS 实现 — LaunchAgent plist
// ============================================================
#elif defined(Q_OS_MACOS)

bool AutoStartManager::setAutoStart(bool enable)
{
    const QString plistDir = QDir::homePath()
        + QStringLiteral("/Library/LaunchAgents");
    const QString plistPath = plistDir + QStringLiteral("/com.crossremotedesktop.plist");

    if (enable) {
        // 确保目录存在
        QDir().mkpath(plistDir);

        // 写入 plist 文件
        QFile file(plistPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_lastError = QStringLiteral("无法创建 LaunchAgent plist: %1")
                .arg(file.errorString());
            qCWarning(lcCoreConfig) << "AutoStartManager:" << m_lastError;
            return false;
        }

        QXmlStreamWriter xml(&file);
        xml.setAutoFormatting(true);
        xml.writeStartDocument();
        xml.writeDTD(QStringLiteral(
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"));
        xml.writeStartElement("plist");
        xml.writeAttribute("version", "1.0");
        xml.writeStartElement("dict");

        xml.writeTextElement("key", "Label");
        xml.writeTextElement("string", applicationName());

        xml.writeTextElement("key", "ProgramArguments");
        xml.writeStartElement("array");
        xml.writeTextElement("string", applicationPath());
        xml.writeEndElement(); // array

        xml.writeTextElement("key", "RunAtLoad");
        xml.writeStartElement("true");
        xml.writeEndElement();

        xml.writeEndElement(); // dict
        xml.writeEndElement(); // plist
        xml.writeEndDocument();
        file.close();

        qCInfo(lcCoreConfig) << "AutoStartManager: 已注册 LaunchAgent 开机自启动";
        return true;
    } else {
        QFile file(plistPath);
        if (file.exists() && !file.remove()) {
            m_lastError = QStringLiteral("无法删除 LaunchAgent plist: %1")
                .arg(file.errorString());
            qCWarning(lcCoreConfig) << "AutoStartManager:" << m_lastError;
            return false;
        }
        qCInfo(lcCoreConfig) << "AutoStartManager: 已取消 LaunchAgent 开机自启动";
        return true;
    }
}

bool AutoStartManager::isAutoStartEnabled() const
{
    const QString plistPath = QDir::homePath()
        + QStringLiteral("/Library/LaunchAgents/com.crossremotedesktop.plist");

    QFile file(plistPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QString currentPath = applicationPath();
    QXmlStreamReader xml(&file);

    bool inProgramArgs = false;
    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == QStringLiteral("key")
                && xml.readElementText() == QStringLiteral("ProgramArguments")) {
                inProgramArgs = true;
            } else if (inProgramArgs && xml.name() == QStringLiteral("string")) {
                QString registeredPath = xml.readElementText();
                file.close();
                return (QString::compare(
                    QDir::toNativeSeparators(registeredPath),
                    currentPath, Qt::CaseInsensitive) == 0);
            }
        }
    }
    file.close();
    return false;
}

// ============================================================
// Linux 实现 — autostart .desktop 文件
// ============================================================
#else

bool AutoStartManager::setAutoStart(bool enable)
{
    const QString autostartDir = QDir::homePath()
        + QStringLiteral("/.config/autostart");
    const QString desktopPath = autostartDir
        + QStringLiteral("/crossremotedesktop.desktop");

    if (enable) {
        QDir().mkpath(autostartDir);

        QFile file(desktopPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_lastError = QStringLiteral("无法创建 autostart desktop 文件: %1")
                .arg(file.errorString());
            qCWarning(lcCoreConfig) << "AutoStartManager:" << m_lastError;
            return false;
        }

        QTextStream stream(&file);
        // Exec= 路径需要引号包裹，防止含空格的路径被拆分执行
        const QString quotedPath = QStringLiteral("\"%1\"").arg(applicationPath());
        stream << "[Desktop Entry]\n"
               << "Type=Application\n"
               << "Name=" << applicationName() << "\n"
               << "Exec=" << quotedPath << "\n"
               << "Terminal=false\n"
               << "Hidden=false\n"
               << "X-GNOME-Autostart-enabled=true\n";
        stream.flush();  // QTextStream 析构在 file.close() 之后，需显式刷新
        file.close();

        qCInfo(lcCoreConfig) << "AutoStartManager: 已注册 autostart 开机自启动";
        return true;
    } else {
        QFile file(desktopPath);
        if (file.exists() && !file.remove()) {
            m_lastError = QStringLiteral("无法删除 autostart desktop 文件: %1")
                .arg(file.errorString());
            qCWarning(lcCoreConfig) << "AutoStartManager:" << m_lastError;
            return false;
        }
        qCInfo(lcCoreConfig) << "AutoStartManager: 已取消 autostart 开机自启动";
        return true;
    }
}

bool AutoStartManager::isAutoStartEnabled() const
{
    const QString desktopPath = QDir::homePath()
        + QStringLiteral("/.config/autostart/crossremotedesktop.desktop");

    QFile file(desktopPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QString currentPath = applicationPath();
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.startsWith(QStringLiteral("Exec="))) {
            // Exec= 路径可能带引号，剥离后比较
            QString registeredPath = line.mid(5).trimmed();
            if (registeredPath.startsWith('\"') && registeredPath.endsWith('\"')) {
                registeredPath = registeredPath.mid(1, registeredPath.size() - 2);
            }
            file.close();
            return (QString::compare(
                QDir::toNativeSeparators(registeredPath),
                currentPath, Qt::CaseSensitive) == 0);
        }
    }
    file.close();
    return false;
}

#endif
