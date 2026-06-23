#pragma once

#include <QtWidgets/QDialog>
#include <QtCore/QString>
#include <QtCore/QSettings>

QT_BEGIN_NAMESPACE
namespace Ui { class SettingsDialog; }
QT_END_NAMESPACE

/**
 * @brief 应用程序偏好设置对话框（即时生效模式）
 */
class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit SettingsDialog(QWidget* parent = nullptr);
	~SettingsDialog();

protected:
	void changeEvent(QEvent* event) override;

private slots:
	// 即时生效槽
	void onLanguageChanged(int index);
	void onAutoStartChanged(bool checked);
	void onListenPortChanged(int value);
	void onUsernameChanged();
	void onPasswordChanged();
	void onLogLevelChanged(int index);
	void onLogRulesChanged();

	// 按钮槽
	void onRestoreDefaultsClicked();
	void onTogglePasswordClicked();
	void onPresetDebugClicked();
	void onResetRulesClicked();

private:
	void setupUI();
	void setupConnections();
	void loadSettings();

	void updateLanguageList();
	void applyLogRules();

	Ui::SettingsDialog* ui;
	QSettings* m_settings;
	QString m_cachedPassword;
	QAction* m_togglePasswordAction = nullptr;
};
