#pragma once

#include <QtCore/QString>
#include <QtWidgets/QDialog>

class AutoStartManager;
class QAction;
class SettingsManager;

QT_BEGIN_NAMESPACE
namespace Ui { class SettingsDialog; }
QT_END_NAMESPACE

/**
 * @brief 应用程序偏好设置对话框（即时生效模式）
 */
class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit SettingsDialog(SettingsManager *settings,
	                        AutoStartManager *autoStartMgr,
	                        QWidget *parent = nullptr);
	~SettingsDialog();

protected:
	void changeEvent(QEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private slots:
	// 即时生效槽
	void onLanguageChanged(int index);
	void onAutoStartChanged(bool checked);
	void onCloseToTrayChanged(bool checked);
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
	SettingsManager* m_settings;
	AutoStartManager* m_autoStartMgr;
	QString m_cachedPassword;
	QString m_lastLang; // 防重入：跳过重复的语言切换请求
	QAction* m_togglePasswordAction = nullptr;
};
