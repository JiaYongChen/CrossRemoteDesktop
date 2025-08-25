#include "logging_categories.h"

// Core/common categories - enable debug for development
Q_LOGGING_CATEGORY(lcApp, "app", QtDebugMsg)
Q_LOGGING_CATEGORY(lcProtocol, "core.protocol", QtDebugMsg)
Q_LOGGING_CATEGORY(lcCompression, "core.compression", QtDebugMsg)
Q_LOGGING_CATEGORY(lcEncryption, "core.encryption", QtDebugMsg)

// Server-side categories - enable debug for development
Q_LOGGING_CATEGORY(lcServer, "server", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerManager, "server.manager", QtDebugMsg)
Q_LOGGING_CATEGORY(lcCapture, "server.capture", QtDebugMsg)
Q_LOGGING_CATEGORY(lcNetServer, "server.net", QtDebugMsg)

// Client-side categories - enable debug for development
Q_LOGGING_CATEGORY(lcClient, "client", QtDebugMsg)
Q_LOGGING_CATEGORY(lcWindowClient, "client.window", QtDebugMsg)
