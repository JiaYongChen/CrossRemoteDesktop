#include "LoggingCategories.h"

// === app ===
Q_LOGGING_CATEGORY(lcApp, "app", QtDebugMsg)

// === core.* ===
Q_LOGGING_CATEGORY(lcCoreProtocol, "core.protocol", QtDebugMsg)
Q_LOGGING_CATEGORY(lcCoreThreading, "core.threading", QtDebugMsg)
Q_LOGGING_CATEGORY(lcCoreConfig, "core.config", QtDebugMsg)

// === server.* ===
Q_LOGGING_CATEGORY(lcServer, "server", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerNetwork, "server.network", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerCapture, "server.capture", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerCaptureDxgi, "server.capture.dxgi", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerEncode, "server.encode", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerQueue, "server.queue", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerClientHandler, "server.clienthandler", QtDebugMsg)
Q_LOGGING_CATEGORY(lcServerInput, "server.input", QtDebugMsg)

// === client.* ===
Q_LOGGING_CATEGORY(lcClient, "client", QtDebugMsg)
Q_LOGGING_CATEGORY(lcClientSession, "client.session", QtDebugMsg)
Q_LOGGING_CATEGORY(lcClientSessionDecode, "client.session.decode", QtDebugMsg)
Q_LOGGING_CATEGORY(lcClientSessionProtocol, "client.session.protocol", QtDebugMsg)
Q_LOGGING_CATEGORY(lcClientGL, "client.gl", QtDebugMsg)
Q_LOGGING_CATEGORY(lcClientRemoteWindow, "client.remotewindow", QtDebugMsg)

// === ui.* ===
Q_LOGGING_CATEGORY(lcUI, "ui", QtDebugMsg)
Q_LOGGING_CATEGORY(lcUIMainWindow, "ui.mainwindow", QtDebugMsg)
Q_LOGGING_CATEGORY(lcUISettingsDialog, "ui.settingsdialog", QtDebugMsg)

// === test.* ===
Q_LOGGING_CATEGORY(lcTest, "test", QtDebugMsg)
Q_LOGGING_CATEGORY(lcTestScreenCapture, "test.screencapture", QtDebugMsg)
Q_LOGGING_CATEGORY(lcTestScreenCaptureIntegration, "test.screencapture.integration", QtDebugMsg)
Q_LOGGING_CATEGORY(lcTestClientHandler, "test.clienthandler", QtDebugMsg)
Q_LOGGING_CATEGORY(lcTestProducerConsumer, "test.producerconsumer", QtDebugMsg)
