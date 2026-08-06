#pragma once

#include <QtCore/QString>
#include <QtWidgets/QDialog>

#include "common/data/ConnectionParams.h"

class QAction;

QT_BEGIN_NAMESPACE
namespace Ui { class ConnectionDialog; }
QT_END_NAMESPACE

class ConnectionDialog : public QDialog {
	Q_OBJECT

public:
	explicit ConnectionDialog(QWidget* parent = nullptr);
	~ConnectionDialog();

	void setDefaultPort(int port);
	void setUsername(const QString& username);

	/// 重置全部控件到默认状态（新建连接每次打开时调用，清除上次残留输入）
	void resetToDefaults();

	/// 从 ConnectionParams 设置全部控件值
	void setConnectionParams(const ConnectionParams& params);
	/// 从控件读取并构建 ConnectionParams（内部复用已有 getter）
	ConnectionParams getConnectionParams() const;

	/// 设置编辑模式：编辑历史记录时隐藏密码可见性切换按钮
	void setEditingMode(bool editing);

	QString getHostname() const;
	QString getHostAddress() const;
	int getPort() const;
	QString getUsername() const;
	QString getPassword() const;

	int getColorDepth() const;
	bool getFullScreen() const;
	int getWindowWidth() const;
	int getWindowHeight() const;
	int getImageQuality() const;

	bool getViewOnly() const;
	bool getShareClipboard() const;
	bool getShowCursor() const;

	int getConnectionTimeout() const;
	bool getAutoReconnect() const;
	int getReconnectInterval() const;

	/// 校验当前主机地址/端口输入，失败时填充错误信息
	bool validateConnectionInfo(QString& errorMessage) const;
	/// 解析 "host[:port]" 输入；未携带端口时回退 defaultPort（纯函数）
	static void parseHostPort(const QString& input, int defaultPort,
	                          QString& host, int& port);

protected:
	void accept() override;
	void changeEvent(QEvent* event) override;
	void showEvent(QShowEvent* event) override;

private slots:
	void onTogglePasswordClicked();
	void onFullScreenToggled(bool checked);

private:
	void setupConnections();
	void retranslateButtons();

	Ui::ConnectionDialog* ui;
	int m_defaultPort;
	QAction* m_togglePasswordAction = nullptr;
};
