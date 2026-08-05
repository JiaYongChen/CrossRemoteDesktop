#include "ConnectionLifecycle.h"

#include <QtCore/QTimer>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include "common/logging/LoggingCategories.h"

namespace {
/// 将 hex 指纹格式化为 SSH 风格冒号分隔大写对（AB:CD:…），便于人工核对
QString formatFingerprint(const QString& hex) {
    const QString upper = hex.toUpper();
    QString out;
    for (int i = 0; i < upper.size(); i += 2) {
        if (!out.isEmpty()) {
            out += ':';
        }
        out += upper.mid(i, 2);
    }
    return out;
}
} // namespace

ConnectionLifecycle::ConnectionLifecycle(QObject* parent)
    : QObject(parent) {
}

void ConnectionLifecycle::manage(QWidget* window) {
    m_window = window;
}

// ── 事件槽（各一行，直接映射 ConnectionManager 语义化事件）──
void ConnectionLifecycle::onConnecting()    { setDisplayState(DisplayState::Connecting); }
void ConnectionLifecycle::onConnected()     { setDisplayState(DisplayState::Connected); }
void ConnectionLifecycle::onDisconnected()  { setDisplayState(DisplayState::Disconnected); }
void ConnectionLifecycle::onReconnecting()  { setDisplayState(DisplayState::Reconnecting); }
void ConnectionLifecycle::onErrorOccurred(const RdError&) { setDisplayState(DisplayState::Error); }

void ConnectionLifecycle::setDisplayState(DisplayState state) {
    if (m_displayState == state) return;

    m_displayState = state;
    updateWindowTitle();

    // 曾建立会话标记：进入 Connected 即置位——区分"真实会话丢失"（弹断连对话框）
    // 与"初始连接失败"（静默关窗）
    if (state == DisplayState::Connected) {
        m_wasAuthenticated = true;
    }

    // 状态离开 Connecting（信任验证期）时无条件收起信任对话框：无论连接恢复（→ Connected）、
    // 转入重连（→ Reconnecting）还是连接终结（→ Disconnected/Error）——
    // 陈旧 MITM 警告不得滞留于健康会话之上，否则其过期「取消」点击会误关健康窗口
    if (m_displayState != DisplayState::Connecting && m_trustDialog) {
        dismissTrustDialog();
    }

    // 终态处理：曾建立会话 → 弹断连对话框；从未建立 → 静默关闭窗口
    if (state == DisplayState::Disconnected || state == DisplayState::Error) {
        if (m_wasAuthenticated) {
            qCInfo(lcClientRemoteWindow) << "ConnectionLifecycle: Connection lost, scheduling disconnection dialog";

            QTimer::singleShot(100, this, [this]() {
                showDisconnectionDialog();
            });
        } else {
            qCInfo(lcClientRemoteWindow) << "ConnectionLifecycle: Pre-auth connection failed, closing window silently";

            QTimer::singleShot(100, this, [this]() {
                if (m_window) {
                    m_window->close();
                }
            });
        }
    }
}

void ConnectionLifecycle::setHostName(const QString& name) {
    if (!name.isEmpty()) {
        m_hostName = name;
        if (m_window) {
            m_window->setWindowTitle(name);
        }
    }
}

void ConnectionLifecycle::updateWindowTitle() {
    if (m_hostName.isEmpty() || !m_window) return;

    QString title;
    switch (m_displayState) {
        case DisplayState::Connecting:
            title = tr("%1 - %2").arg(m_hostName, tr("正在连接..."));
            break;
        case DisplayState::Connected:
            title = tr("%1 - %2").arg(m_hostName, tr("已连接"));
            break;
        case DisplayState::Reconnecting:
            title = tr("%1 - %2").arg(m_hostName, tr("正在重连..."));
            break;
        case DisplayState::Disconnected:
            title = tr("%1 - %2").arg(m_hostName, tr("未连接"));
            break;
        case DisplayState::Error:
            title = tr("%1 - %2").arg(m_hostName, tr("连接错误"));
            break;
        default:
            title = m_hostName;
            break;
    }
    if (m_viewOnly) {
        title = tr("%1 [仅查看]").arg(title);
    }
    m_window->setWindowTitle(title);
}

void ConnectionLifecycle::showDisconnectionDialog() {
    if (!m_window || m_dialogShowing) return;

    m_dialogShowing = true;

    qCInfo(lcClientRemoteWindow) << "ConnectionLifecycle: Showing disconnection dialog";

    QMessageBox msgBox(m_window);
    msgBox.setWindowTitle(tr("连接已断开"));
    msgBox.setText(tr("与远程主机 %1 的连接已断开。")
        .arg(m_hostName.isEmpty() ? tr("服务器") : m_hostName));
    msgBox.setInformativeText(tr("窗口即将关闭。"));
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();

    qCInfo(lcClientRemoteWindow) << "ConnectionLifecycle: User confirmed disconnect, closing window";
    m_window->close();
}

void ConnectionLifecycle::dismissTrustDialog() {
    if (!m_trustDialog) return;
    // 先断开决策回环再销毁：任何迟到的 finished 不得补发陈旧决策或触碰新状态
    disconnect(m_trustDialog, &QDialog::finished, this, nullptr);
    m_trustDialog->deleteLater();
    m_trustDialog = nullptr;
}

void ConnectionLifecycle::showTrustWarning(const QString& endpoint,
                                           const QString& oldFingerprint,
                                           const QString& newFingerprint) {
    if (!m_window) return;

    // 重入（挂起决策期间指纹再次变更）：替换对话框，呈现最新指纹
    if (m_trustDialog) {
        dismissTrustDialog();
    }

    qCWarning(lcClientRemoteWindow) << "ConnectionLifecycle: 服务端身份变更，弹出信任警告" << endpoint;

    // 刻意非模态：不用 exec() 嵌套事件循环（模态会让 socket 事件在对话框栈帧"内部"
    // 被投递，窗口可能在对话框返回前被删除 → use-after-free，且重连产生的新指纹无法刷新对话框）。
    // parent 到窗口：窗口销毁时对话框随之删除；finished 以 this 为上下文连接，
    // this 随窗口销毁时连接自动断开——两条路径都不会触及已释放对象
    auto* dialog = new QDialog(m_window);
    m_trustDialog = dialog;
    dialog->setWindowTitle(tr("服务端身份已变更"));
    dialog->setMinimumWidth(420);

    auto* layout = new QVBoxLayout(dialog);

    auto* warnLabel = new QLabel(tr("警告：无法确认服务端身份"));
    warnLabel->setStyleSheet("color: #d32f2f; font-weight: bold;");
    layout->addWidget(warnLabel);

    layout->addWidget(new QLabel(tr(
        "主机 %1 的证书指纹与上次记录不一致。\n"
        "这可能是中间人攻击，也可能是服务端合法重装或更换证书。\n"
        "除非你确认服务端近期重装过，否则不要继续。").arg(endpoint)));

    layout->addWidget(new QLabel(tr("原指纹：\n%1").arg(formatFingerprint(oldFingerprint))));
    layout->addWidget(new QLabel(tr("新指纹：\n%1").arg(formatFingerprint(newFingerprint))));

    auto* buttonLayout = new QHBoxLayout();
    auto* trustBtn = new QPushButton(tr("信任新证书并连接"));
    auto* cancelBtn = new QPushButton(tr("取消"));
    buttonLayout->addStretch();
    buttonLayout->addWidget(trustBtn);
    buttonLayout->addWidget(cancelBtn);
    layout->addLayout(buttonLayout);

    QObject::connect(trustBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, dialog, &QDialog::reject);

    QObject::connect(dialog, &QDialog::finished, this, [this, dialog](int result) {
        m_trustDialog = nullptr;
        dialog->deleteLater();
        const bool accept = (result == QDialog::Accepted);
        // 决策有效性以发射时刻的状态为准：仅挂起期内的决策有效。
        // 状态已离开挂起态（连接恢复/重连/终结）时，本决策已失效——见 setDisplayState 的收起调用点；
        // 此检查是防御纵深，确保任何迟到的 finished 都不得触碰窗口
        const bool wasPending = (m_displayState == DisplayState::Connecting);
        // 直连：ConnectionManager 同步处置（若已离开挂起态则忽略），
        // 返回后本对象缓存的状态即为最新
        emit trustDecision(accept);
        // 仅当拒绝且决策实际生效（仍在挂起期）时关闭窗口；
        // 迟到的「取消」点击（状态已恢复/终结）不得误关健康或正在收尾的窗口
        if (!accept && wasPending) {
            if (m_window) {
                m_window->close();
            }
        }
    });

    dialog->adjustSize();
    if (m_window->isVisible()) {
        dialog->move(m_window->geometry().center() - dialog->rect().center());
    }
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}
