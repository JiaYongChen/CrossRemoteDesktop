<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en_US">
<context>
    <name>ClientManager</name>
    <message>
        <source>服务器错误</source>
        <translation type="vanished">Server Error</translation>
    </message>
    <message>
        <source>连接服务器时发生错误：%1</source>
        <translation type="vanished">Error connecting to server: %1</translation>
    </message>
</context>
<context>
    <name>ClientRemoteWindow</name>
    <message>
        <location filename="../../src/client/window/ClientRemoteWindow.cpp" line="48"/>
        <source>Remote Desktop</source>
        <translation>Remote Desktop</translation>
    </message>
</context>
<context>
    <name>ConnectionCard</name>
    <message>
        <location filename="../../src/common/windows/ConnectionCard.cpp" line="65"/>
        <location filename="../../src/common/windows/ConnectionCard.cpp" line="133"/>
        <source>连接</source>
        <translation type="unfinished">Connection</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/ConnectionCard.cpp" line="71"/>
        <location filename="../../src/common/windows/ConnectionCard.cpp" line="134"/>
        <source>修改参数</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/ConnectionCard.cpp" line="77"/>
        <location filename="../../src/common/windows/ConnectionCard.cpp" line="135"/>
        <source>删除记录</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>ConnectionDialog</name>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="6"/>
        <source>远程桌面连接</source>
        <translation>Remote Desktop Connection</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="21"/>
        <source>/* ============================================================
 * ConnectionDialog — Fluent Design (WinUI) 浅色主题
 * ============================================================ */

/* --- 窗口 --- */
QDialog#ConnectionDialog {
    background-color: #f3f3f3;
    font-family: &quot;Segoe UI Variable&quot;, &quot;Segoe UI&quot;, &quot;Microsoft YaHei&quot;, sans-serif;
    font-size: 13px;
    color: #1a1a1a;
}

/* --- 标签页容器（继承对话框背景） --- */
QWidget#tabContainer {
    background-color: transparent;
}

/* --- 标签页容器（标签栏区域背景） --- */
QTabWidget {
    background-color: transparent;
}

/* --- 标签页内容区 --- */
QTabWidget::pane {
    background-color: #fcfcfc;
    border: none;
    border-top: 1px solid #e0e0e0;
    top: -1px;
    padding: 6px 28px;
}

/* --- 标签栏 --- */
QTabBar::tab {
    background-color: transparent;
    color: #666666;
    padding: 10px 20px;
    border: none;
    border-bottom: 3px solid transparent;
    font-size: 13px;
    font-weight: 400;
}

QTabBar::tab:selected {
    color: #1a1a1a;
    font-weight: 600;
    border-bottom: 3px solid #0078d4;
}

QTabBar::tab:hover:!selected {
    color: #1a1a1a;
}

/* --- 分组框 → 白色卡片 --- */
QGroupBox {
    background-color: #ffffff;
    border: 1px solid #e8e8e8;
    border-radius: 8px;
    margin-top: 0px;
    padding: 18px 12px 12px 12px;
    font-weight: 600;
    font-size: 13px;
    color: #1a1a1a;
}

QGroupBox::title {
    subcontrol-origin: margin;
    left: 14px;
    padding: 0 6px 0 6px;
    color: #1a1a1a;
}

/* --- 标签 --- */
QLabel {
    color: #1a1a1a;
    font-weight: 400;
    font-size: 13px;
}

QLabel:disabled {
    color: #a0a0a0;
}

/* --- 下拉框 --- */
QComboBox {
    border: 1px solid #cfcfcf;
    border-radius: 4px;
    padding: 5px 10px;
    background-color: #ffffff;
    color: #1a1a1a;
    font-size: 13px;
    font-family: &quot;Segoe UI&quot;, sans-serif;
}

QComboBox:hover {
    border-color: #999999;
}

QComboBox:focus {
    border-color: #0078d4;
}

QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 24px;
    border: none;
    border-top-right-radius: 4px;
    border-bottom-right-radius: 4px;
}

QComboBox::down-arrow {
    image: url(:/icons/dropdown-arrow.svg);
    width: 12px;
    height: 12px;
    margin-right: 6px;
}

QComboBox QAbstractItemView {
    border: 1px solid #cfcfcf;
    border-radius: 4px;
    background-color: #ffffff;
    selection-background-color: rgba(0, 120, 212, 0.12);
    selection-color: #1a1a1a;
    outline: none;
}

/* --- 数字输入框 --- */
QSpinBox {
    border: 1px solid #cfcfcf;
    border-radius: 4px;
    padding: 8px 12px;
    background-color: #ffffff;
    color: #1a1a1a;
    font-size: 13px;
    font-family: &quot;Segoe UI&quot;, sans-serif;
}

QSpinBox:hover {
    border-color: #999999;
}

QSpinBox:focus {
    border-color: #0078d4;
}

QSpinBox:disabled {
    background-color: #f0f0f0;
    color: #a0a0a0;
    border-color: #e0e0e0;
}

QSpinBox::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
    width: 20px;
    border-left: 1px solid #cfcfcf;
    border-bottom: 1px solid #cfcfcf;
    border-top-right-radius: 4px;
}

QSpinBox::up-button:hover {
    background-color: #e8e8e8;
}

QSpinBox::up-arrow {
    image: url(:/icons/spin-up.svg);
    width: 8px;
    height: 8px;
}

QSpinBox::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
    width: 20px;
    border-left: 1px solid #cfcfcf;
    border-bottom-right-radius: 4px;
}

QSpinBox::down-button:hover {
    background-color: #e8e8e8;
}

QSpinBox::down-arrow {
    image: url(:/icons/spin-down.svg);
    width: 8px;
    height: 8px;
}

/* --- 文本框 --- */
QLineEdit {
    border: 1px solid #cfcfcf;
    border-radius: 4px;
    padding: 8px 12px;
    background-color: #ffffff;
    color: #1a1a1a;
    font-size: 13px;
    font-family: &quot;Segoe UI&quot;, sans-serif;
}

QLineEdit:hover {
    border-color: #999999;
}

QLineEdit:focus {
    border-color: #0078d4;
}

/* --- 复选框（传统方框样式） --- */
QCheckBox {
    color: #1a1a1a;
    font-size: 13px;
    spacing: 8px;
}

QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border-radius: 3px;
    background-color: #ffffff;
    border: 2px solid #cccccc;
}

QCheckBox::indicator:checked {
    background-color: #0078d4;
    border-color: #0078d4;
}

QCheckBox::indicator:hover {
    border-color: #999999;
}

QCheckBox::indicator:checked:hover {
    background-color: #106ebe;
    border-color: #106ebe;
}

/* --- 滑块（图像质量） --- */
QSlider::groove:horizontal {
    background-color: #e0e0e0;
    height: 4px;
    border-radius: 2px;
}

QSlider::sub-page:horizontal {
    background-color: #0078d4;
    height: 4px;
    border-radius: 2px;
}

QSlider::handle:horizontal {
    background-color: #ffffff;
    border: 2px solid #0078d4;
    width: 16px;
    height: 16px;
    margin: -6px 0;
    border-radius: 8px;
}

QSlider::handle:horizontal:hover {
    background-color: #f0f0f0;
    border-color: #106ebe;
}

/* --- 底部按钮栏（对话框按钮） --- */
QDialogButtonBox QPushButton {
    background-color: #0078d4;
    color: #ffffff;
    border: none;
    border-radius: 4px;
    padding: 6px 18px;
    font-size: 13px;
    font-weight: 500;
    min-width: 90px;
    min-height: 28px;
}

QDialogButtonBox QPushButton:hover {
    background-color: #106ebe;
}

QDialogButtonBox QPushButton:pressed {
    background-color: #005a9e;
}

/* --- 滚动条 --- */
QScrollBar:vertical {
    width: 6px;
    background-color: transparent;
    margin: 0;
}

QScrollBar::handle:vertical {
    background-color: #c0c0c0;
    border-radius: 3px;
    min-height: 30px;
}

QScrollBar::handle:vertical:hover {
    background-color: #a0a0a0;
}

QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
    height: 0px;
}

QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical {
    background-color: transparent;
}

/* --- 标签页内 QWidget 透底 --- */
QWidget#connectionTab,
QWidget#optionsTab,
QWidget#advancedTab {
    background-color: transparent;
}</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="358"/>
        <source>连接信息</source>
        <translation>Connection Info</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="361"/>
        <source>服务器信息</source>
        <translation>Server Information</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="365"/>
        <source>主机名:</source>
        <translation>Hostname:</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="366"/>
        <source>可选的友好名称（如&quot;办公室电脑&quot;）</source>
        <translation>Optional friendly name (e.g. &quot;Office PC&quot;)</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="367"/>
        <source>主机地址:</source>
        <translation>Host Address:</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="373"/>
        <source>身份验证</source>
        <translation>Authentication</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="377"/>
        <source>用户名:</source>
        <translation>Username:</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="378"/>
        <source>输入用户名（可选）</source>
        <translation>Enter username (optional)</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="379"/>
        <source>密码:</source>
        <translation>Password:</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="380"/>
        <source>输入密码（可选）</source>
        <translation>Enter password (optional)</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="368"/>
        <source>IP 或主机名，可含端口如 192.168.1.100:5921</source>
        <translation>IP or hostname, optionally with port</translation>
    </message>
    <message>
        <source>👁</source>
        <translation type="vanished">[TODO]</translation>
    </message>
    <message>
        <source>显示/隐藏密码</source>
        <translation type="vanished">Show/Hide Password</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="388"/>
        <source>显示 &amp;&amp; 功能</source>
        <translation>Display &amp; Function</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="391"/>
        <source>显示设置</source>
        <translation>Display Settings</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="395"/>
        <source>颜色深度:</source>
        <translation>Color Depth:</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="396"/>
        <source>16位 (65K 颜色)</source>
        <translation>16-bit (65K colors)</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="396"/>
        <source>24位 (16M 颜色)</source>
        <translation>24-bit (16M colors)</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="396"/>
        <source>32位 (真彩色)</source>
        <translation>32-bit (True Color)</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="398"/>
        <source>全屏模式</source>
        <translation>Full Screen Mode</translation>
    </message>
    <message>
        <source>窗口大小</source>
        <translation type="vanished">Window Size</translation>
    </message>
    <message>
        <source> px</source>
        <translation type="vanished">Fullscreen Mode</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="402"/>
        <source>×</source>
        <translation>[TODO]</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="410"/>
        <source>图像质量</source>
        <translation>Image Quality</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="421"/>
        <source>仅查看模式（禁用输入）</source>
        <translation>View Only Mode (disable input)</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="428"/>
        <source>网络</source>
        <translation>Network</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="417"/>
        <source>功能选项</source>
        <translation>Feature Options</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="397"/>
        <source>显示模式:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="419"/>
        <source>启用剪贴板同步</source>
        <translation>Enable Clipboard Sync</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="420"/>
        <source>显示远程光标</source>
        <translation>Show Remote Cursor</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="431"/>
        <source>网络设置</source>
        <translation>Network Settings</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="435"/>
        <source>连接超时:</source>
        <translation>Connection Timeout:</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="436"/>
        <location filename="../../src/ui/ConnectionDialog.ui" line="439"/>
        <source> 秒</source>
        <translation> sec</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="437"/>
        <source>自动重连</source>
        <translation>Auto Reconnect</translation>
    </message>
    <message>
        <location filename="../../src/ui/ConnectionDialog.ui" line="438"/>
        <source>重连间隔:</source>
        <translation>Reconnect Interval:</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/ConnectionDialog.cpp" line="82"/>
        <source>连接</source>
        <translation>Connection</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/ConnectionDialog.cpp" line="94"/>
        <source>主机地址不能包含空格</source>
        <translation>Host address cannot contain spaces</translation>
    </message>
</context>
<context>
    <name>ConnectionLifecycle</name>
    <message>
        <location filename="../../src/client/window/ConnectionLifecycle.cpp" line="61"/>
        <location filename="../../src/client/window/ConnectionLifecycle.cpp" line="64"/>
        <location filename="../../src/client/window/ConnectionLifecycle.cpp" line="67"/>
        <location filename="../../src/client/window/ConnectionLifecycle.cpp" line="70"/>
        <location filename="../../src/client/window/ConnectionLifecycle.cpp" line="73"/>
        <location filename="../../src/client/window/ConnectionLifecycle.cpp" line="76"/>
        <location filename="../../src/client/window/ConnectionLifecycle.cpp" line="79"/>
        <location filename="../../src/client/window/ConnectionLifecycle.cpp" line="82"/>
        <source>%1 - %2</source>
        <translation>%1 - %2</translation>
    </message>
    <message>
        <location filename="../../src/client/window/ConnectionLifecycle.cpp" line="97"/>
        <source>Connection Disconnected</source>
        <translation>Connection Disconnected</translation>
    </message>
    <message>
        <location filename="../../src/client/window/ConnectionLifecycle.cpp" line="98"/>
        <source>Connection to remote host %1 has been disconnected.</source>
        <translation>Connection to remote host %1 has been disconnected.</translation>
    </message>
    <message>
        <location filename="../../src/client/window/ConnectionLifecycle.cpp" line="100"/>
        <source>The window will close.</source>
        <translation>The window will close.</translation>
    </message>
</context>
<context>
    <name>HamburgerMenu</name>
    <message>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="26"/>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="202"/>
        <source>菜单</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="46"/>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="203"/>
        <source>新建连接 (Ctrl+N)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="49"/>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="204"/>
        <source>连接 (Ctrl+O)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="58"/>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="205"/>
        <source>设置 (Ctrl+,)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="67"/>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="206"/>
        <source>关于</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="70"/>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="207"/>
        <source>退出 (Ctrl+Q)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="92"/>
        <location filename="../../src/common/windows/HamburgerMenu.cpp" line="208"/>
        <source>切换主题</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="14"/>
        <location filename="../../src/ui/mainwindow.ui" line="95"/>
        <location filename="../../src/common/windows/MainWindow.cpp" line="95"/>
        <location filename="../../src/common/windows/MainWindow.cpp" line="274"/>
        <location filename="../../src/common/windows/MainWindow.cpp" line="943"/>
        <source>Qt远程桌面</source>
        <translation>Cross Remote Desktop</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="110"/>
        <source>跨平台远程桌面解决方案</source>
        <translation>Cross-platform Remote Desktop Solution</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="116"/>
        <source>color: #666;</source>
        <translation>color: #666;</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="167"/>
        <source>连接远程桌面</source>
        <translation>Connect to Remote Desktop</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="180"/>
        <source>QPushButton {
    background-color: #007ACC;
    color: #f0f0f0;
    border: none;
    border-radius: 8px;
    padding: 10px 20px;
}
QPushButton:hover {
    background-color: #005A9E;
}
QPushButton:pressed {
    background-color: #004578;
}</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="212"/>
        <source>启动服务器</source>
        <translation>Start Server</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="225"/>
        <source>QPushButton {
    background-color: #28A745;
    color: #f0f0f0;
    border: none;
    border-radius: 8px;
    padding: 10px 20px;
}
QPushButton:hover {
    background-color: #218838;
}
QPushButton:pressed {
    background-color: #1E7E34;
}</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="272"/>
        <source>最近连接</source>
        <translation>Recent Connections</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="275"/>
        <source>QGroupBox {
    font-weight: bold;
    border: 2px solid #CCCCCC;
    border-radius: 5px;
    margin-top: 10px;
    padding-top: 10px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 5px 0 5px;
}</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="319"/>
        <source>QListWidget {
    border: 1px solid #CCCCCC;
    border-radius: 3px;
    background-color: white;
}
QListWidget::item {
    padding: 8px;
    border-bottom: 1px solid #EEEEEE;
}
QListWidget::item:selected {
    background-color: #E3F2FD;
    color: #1976D2;
}
QListWidget::item:hover {
    background-color: #F5F5F5;
}</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="378"/>
        <source>文件(&amp;F)</source>
        <translation>&amp;File</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="389"/>
        <location filename="../../src/ui/mainwindow.ui" line="503"/>
        <source>连接(&amp;C)</source>
        <translation>&amp;Connection</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="400"/>
        <source>服务器(&amp;S)</source>
        <translation>Server(&amp;S)</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="409"/>
        <source>工具(&amp;T)</source>
        <translation>&amp;Tools</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="421"/>
        <source>视图(&amp;V)</source>
        <translation>View(&amp;V)</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="433"/>
        <source>帮助(&amp;H)</source>
        <translation>&amp;Help</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="455"/>
        <source>工具栏</source>
        <translation>Toolbar</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="488"/>
        <source>新建连接(&amp;N)</source>
        <translation>&amp;New Connection</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="491"/>
        <source>创建新的远程桌面连接</source>
        <translation>Create New Remote Desktop Connection</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="494"/>
        <source>Ctrl+N</source>
        <translation>Ctrl+N</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="506"/>
        <source>连接到远程桌面</source>
        <translation>Connect to remote desktop</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="509"/>
        <source>Ctrl+Shift+C</source>
        <translation>Ctrl+Shift+C</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="521"/>
        <source>断开连接(&amp;D)</source>
        <translation>&amp;Disconnect</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="524"/>
        <source>断开远程桌面连接</source>
        <translation>Disconnect remote desktop connection</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="527"/>
        <source>Ctrl+Shift+D</source>
        <translation>Ctrl+Shift+D</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="536"/>
        <source>启动服务器(&amp;S)</source>
        <translation>&amp;Start Server</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="539"/>
        <source>启动远程桌面服务器</source>
        <translation>Start remote desktop server</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="542"/>
        <location filename="../../src/ui/mainwindow.ui" line="734"/>
        <source>Ctrl+Shift+S</source>
        <translation>Ctrl+Shift+S</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="554"/>
        <source>停止服务器(&amp;T)</source>
        <translation>S&amp;top Server</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="557"/>
        <source>停止远程桌面服务器</source>
        <translation>Stop remote desktop server</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="560"/>
        <source>Ctrl+Shift+T</source>
        <translation>Ctrl+Shift+T</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="569"/>
        <source>设置(&amp;P)</source>
        <translation>&amp;Preferences</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="572"/>
        <source>打开应用程序设置</source>
        <translation>Open application settings</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="575"/>
        <source>Ctrl+P</source>
        <translation>Ctrl+P</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="590"/>
        <source>全屏(&amp;F)</source>
        <translation>&amp;Full Screen</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="593"/>
        <source>切换全屏模式</source>
        <translation>Toggle full screen mode</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="596"/>
        <source>F11</source>
        <translation>F11</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="605"/>
        <source>关于(&amp;A)</source>
        <translation>&amp;About</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="608"/>
        <source>关于Cross Remote Desktop</source>
        <translation>About Cross Remote Desktop</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="617"/>
        <source>退出(&amp;X)</source>
        <translation>E&amp;xit</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="620"/>
        <source>退出应用程序</source>
        <translation>Exit the application</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="623"/>
        <source>Ctrl+Q</source>
        <translation>Ctrl+Q</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="635"/>
        <source>重新连接(&amp;R)</source>
        <translation>&amp;Reconnect</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="638"/>
        <source>重新连接到远程桌面</source>
        <translation>Reconnect to remote desktop</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="641"/>
        <source>Ctrl+R</source>
        <translation>Ctrl+R</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="650"/>
        <source>导入连接(&amp;I)</source>
        <translation>&amp;Import Connection</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="653"/>
        <source>从文件导入连接配置</source>
        <translation>Import connection config from file</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="662"/>
        <source>导出连接(&amp;E)</source>
        <translation>&amp;Export Connection</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="665"/>
        <source>导出连接配置到文件</source>
        <translation>Export connection config to file</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="674"/>
        <source>服务器设置(&amp;S)</source>
        <translation>&amp;Server Settings</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="677"/>
        <source>配置服务器设置</source>
        <translation>Configure server settings</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="689"/>
        <source>文件传输(&amp;F)</source>
        <translation>&amp;File Transfer</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="692"/>
        <source>打开文件传输窗口</source>
        <translation>Open file transfer window</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="695"/>
        <source>Ctrl+T</source>
        <translation>Ctrl+T</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="713"/>
        <source>剪贴板同步(&amp;C)</source>
        <translation>&amp;Clipboard Sync</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="716"/>
        <source>启用/禁用剪贴板同步</source>
        <translation>Enable/disable clipboard sync</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="728"/>
        <source>截图(&amp;S)</source>
        <translation>&amp;Screenshot</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="731"/>
        <source>截取远程桌面屏幕</source>
        <translation>Take a screenshot of the remote desktop</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="749"/>
        <source>录制(&amp;R)</source>
        <translation>&amp;Record</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="752"/>
        <source>开始/停止录制远程桌面</source>
        <translation>Start/stop recording remote desktop</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="755"/>
        <source>Ctrl+Shift+R</source>
        <translation>Ctrl+Shift+R</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="767"/>
        <source>放大(&amp;I)</source>
        <translation>Zoom &amp;In</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="770"/>
        <source>放大远程桌面视图</source>
        <translation>Zoom in remote desktop view</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="773"/>
        <source>Ctrl+=</source>
        <translation>Ctrl+=</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="785"/>
        <source>缩小(&amp;O)</source>
        <translation>Zoom &amp;Out</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="788"/>
        <source>缩小远程桌面视图</source>
        <translation>Zoom out remote desktop view</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="791"/>
        <source>Ctrl+-</source>
        <translation>Ctrl+-</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="803"/>
        <source>适应窗口(&amp;F)</source>
        <translation>&amp;Fit to Window</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="806"/>
        <source>缩放以适应窗口大小</source>
        <translation>Scale to fit window</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="809"/>
        <source>Ctrl+0</source>
        <translation>Ctrl+0</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="821"/>
        <source>实际大小(&amp;A)</source>
        <translation>&amp;Actual Size</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="824"/>
        <source>显示实际大小</source>
        <translation>Show actual size</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="827"/>
        <source>Ctrl+1</source>
        <translation>Ctrl+1</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="838"/>
        <source>显示工具栏(&amp;T)</source>
        <translation>Show &amp;Toolbar</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="841"/>
        <source>显示/隐藏工具栏</source>
        <translation>Show/hide toolbar</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="852"/>
        <source>显示状态栏(&amp;S)</source>
        <translation>Show &amp;Status Bar</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="855"/>
        <source>显示/隐藏状态栏</source>
        <translation>Show/hide status bar</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="864"/>
        <source>用户指南(&amp;U)</source>
        <translation>&amp;User Guide</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="867"/>
        <source>打开用户指南</source>
        <translation>Open user guide</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="870"/>
        <source>F1</source>
        <translation>F1</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="879"/>
        <source>键盘快捷键(&amp;K)</source>
        <translation>&amp;Keyboard Shortcuts</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="882"/>
        <source>显示键盘快捷键列表</source>
        <translation>Show keyboard shortcuts list</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="891"/>
        <source>检查更新(&amp;U)</source>
        <translation>Check for &amp;Updates</translation>
    </message>
    <message>
        <location filename="../../src/ui/mainwindow.ui" line="894"/>
        <source>检查应用程序更新</source>
        <translation>Check for application updates</translation>
    </message>
    <message>
        <source>新建连接(&amp;N)...</source>
        <translation type="vanished">&amp;New Connection...</translation>
    </message>
    <message>
        <source>创建新的远程连接</source>
        <translation type="vanished">Create a new remote connection</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="30"/>
        <source>新建连接 (Ctrl+N)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="34"/>
        <source>Ctrl+O</source>
        <translation>Ctrl+O</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="35"/>
        <source>连接 (Ctrl+O)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="40"/>
        <source>设置 (Ctrl+,)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="45"/>
        <source>退出 (Ctrl+Q)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>连接到远程主机</source>
        <translation type="vanished">Connect to a remote host</translation>
    </message>
    <message>
        <source>设置(&amp;S)...</source>
        <translation type="vanished">&amp;Settings...</translation>
    </message>
    <message>
        <source>配置应用程序设置</source>
        <translation type="vanished">Configure application settings</translation>
    </message>
    <message>
        <source>显示应用程序的关于对话框</source>
        <translation type="vanished">About Cross Remote Desktop</translation>
    </message>
    <message>
        <source>关于Qt(&amp;Q)</source>
        <translation type="vanished">About &amp;Qt</translation>
    </message>
    <message>
        <source>显示Qt库的关于对话框</source>
        <translation type="vanished">About the Qt library</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="280"/>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="49"/>
        <source>最小化(&amp;N)</source>
        <translation>Mi&amp;nimize</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="281"/>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="50"/>
        <source>最大化(&amp;X)</source>
        <translation>Ma&amp;ximize</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="282"/>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="51"/>
        <source>恢复(&amp;R)</source>
        <translation>&amp;Restore</translation>
    </message>
    <message>
        <source>主工具栏</source>
        <translation type="vanished">Main Toolbar</translation>
    </message>
    <message>
        <source>未连接</source>
        <translation type="vanished">Not connected</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="704"/>
        <source>服务器已停止</source>
        <translation>Server stopped</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="66"/>
        <source>CPU: 0% | 内存: 0MB</source>
        <translation>CPU: 0% | Memory: 0MB</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="288"/>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="73"/>
        <source>就绪</source>
        <translation>Ready</translation>
    </message>
    <message>
        <source>欢迎使用Qt远程桌面</source>
        <translation type="vanished">Welcome to Cross Remote Desktop</translation>
    </message>
    <message>
        <source>使用左侧按钮连接到远程计算机。</source>
        <translation type="vanished">Use the buttons on the left to connect to a remote computer.</translation>
    </message>
    <message>
        <source>连接历史记录</source>
        <translation type="vanished">Connection History</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="191"/>
        <source>远程桌面</source>
        <translation>Remote Desktop</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="277"/>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="43"/>
        <source>退出</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="285"/>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="60"/>
        <source>连接：未连接</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="286"/>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="63"/>
        <source>服务器：已停止</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="292"/>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="126"/>
        <source>搜索历史连接...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="297"/>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="167"/>
        <source>无匹配的连接记录</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="298"/>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="133"/>
        <location filename="../../src/common/windows/MainWindowLayout.cpp" line="166"/>
        <source>暂无连接历史</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="353"/>
        <source>需要辅助功能权限</source>
        <translation>Accessibility Permission Required</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="354"/>
        <source>&lt;p&gt;Qt远程桌面需要&lt;b&gt;辅助功能权限&lt;/b&gt;才能模拟鼠标和键盘输入。&lt;/p&gt;&lt;p&gt;请按照以下步骤授予权限：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;系统偏好设置&lt;/b&gt;&lt;/li&gt;&lt;li&gt;选择&lt;b&gt;安全性与隐私&lt;/b&gt;&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;隐私&lt;/b&gt;标签&lt;/li&gt;&lt;li&gt;在左侧列表中选择&lt;b&gt;辅助功能&lt;/b&gt;&lt;/li&gt;&lt;li&gt;点击左下角的锁图标解锁&lt;/li&gt;&lt;li&gt;在右侧列表中勾选&lt;b&gt;CrossRemoteDesktop&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;授予权限后，请重启应用程序。&lt;/p&gt;</source>
        <translation>[TODO]</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="394"/>
        <source>关于Qt远程桌面</source>
        <translation>About Cross Remote Desktop</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="395"/>
        <source>&lt;h2&gt;Qt远程桌面 1.0&lt;/h2&gt;&lt;p&gt;基于Qt 6.9.1构建的跨平台远程桌面应用程序。&lt;/p&gt;&lt;p&gt;支持macOS和Windows系统之间的远程连接。&lt;/p&gt;</source>
        <translation>&lt;h2&gt;Cross Remote Desktop 1.0&lt;/h2&gt;&lt;p&gt;A cross-platform remote desktop application built with Qt 6.9.1.&lt;/p&gt;&lt;p&gt;Supports remote connections between macOS and Windows systems.&lt;/p&gt;</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="401"/>
        <source>关于Qt</source>
        <translation>About Qt</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="402"/>
        <source>&lt;h2&gt;关于Qt&lt;/h2&gt;&lt;p&gt;本程序使用Qt版本6.9.1。&lt;/p&gt;&lt;p&gt;Qt是一个用于跨平台应用程序开发的C++工具包。&lt;/p&gt;&lt;p&gt;Qt为所有主要桌面操作系统提供单一源代码的可移植性。它也可用于嵌入式Linux和其他嵌入式及移动操作系统。&lt;/p&gt;&lt;p&gt;Qt可在多种许可选项下使用，旨在满足我们各种用户的需求。&lt;/p&gt;&lt;p&gt;根据我们的商业许可协议许可的Qt适用于开发专有/商业软件，您不希望与第三方共享任何源代码或无法遵守GNU(L)GPL条款。&lt;/p&gt;&lt;p&gt;根据GNU(L)GPL许可的Qt适用于Qt应用程序的开发，前提是您可以遵守相应许可证的条款和条件。&lt;/p&gt;&lt;p&gt;版权所有 (C) Qt公司有限公司及其他贡献者。&lt;/p&gt;&lt;p&gt;Qt和Qt标志是Qt公司有限公司的商标。&lt;/p&gt;&lt;p&gt;Qt是Qt公司有限公司开发的开源项目产品。&lt;/p&gt;</source>
        <translation>&lt;h2&gt;About Qt&lt;/h2&gt;&lt;p&gt;This program uses Qt version 6.9.1.&lt;/p&gt;&lt;p&gt;Qt is a C++ toolkit for cross-platform application development.&lt;/p&gt;&lt;p&gt;Qt provides single-source portability across all major desktop operating systems. It is also available for embedded Linux and other embedded and mobile operating systems.&lt;/p&gt;&lt;p&gt;Qt is available under multiple licensing options designed to accommodate the needs of our various users.&lt;/p&gt;&lt;p&gt;Qt licensed under our commercial license agreement is appropriate for development of proprietary/commercial software where you do not want to share any source code with third parties or otherwise cannot comply with the terms of the GNU (L)GPL.&lt;/p&gt;&lt;p&gt;Qt licensed under the GNU (L)GPL is appropriate for the development of Qt applications provided you can comply with the terms and conditions of the respective licenses.&lt;/p&gt;&lt;p&gt;Copyright (C) The Qt Company Ltd. and other contributors.&lt;/p&gt;&lt;p&gt;Qt and the Qt logo are trademarks of The Qt Company Ltd.&lt;/p&gt;&lt;p&gt;Qt is an open source project product developed by The Qt Company Ltd.&lt;/p&gt;</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="472"/>
        <source>CPU: --% | 内存: %1 MB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="505"/>
        <source>CPU: %1% | 内存: %2 MB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="524"/>
        <source>CPU: %1%</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="538"/>
        <source>内存: %1 MB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="692"/>
        <source>服务器启动成功，端口: %1</source>
        <translation>Server started successfully, port: %1</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="886"/>
        <source>确定删除此连接记录？</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>停止服务器</source>
        <translation type="vanished">Stop Server</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="709"/>
        <source>服务器启动失败</source>
        <translation>Server failed to start</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="710"/>
        <source>服务器错误</source>
        <translation>Server Error</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="715"/>
        <source>客户端已连接: %1</source>
        <translation>Client connected: %1</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="720"/>
        <source>客户端已断开: %1</source>
        <translation>Client disconnected: %1</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="725"/>
        <source>客户端已认证: %1</source>
        <translation>Client authenticated: %1</translation>
    </message>
    <message>
        <source>已删除连接记录</source>
        <translation type="vanished">Connection record deleted</translation>
    </message>
    <message>
        <source>连接</source>
        <translation type="vanished">Connection</translation>
    </message>
    <message>
        <source>删除</source>
        <translation type="vanished">Delete</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="885"/>
        <source>确认删除</source>
        <translation>Confirm Delete</translation>
    </message>
    <message>
        <source>确定要删除连接记录 &quot;%1&quot; 吗？</source>
        <translation type="vanished">Are you sure you want to delete connection record &quot;%1&quot;?</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/MainWindow.cpp" line="274"/>
        <location filename="../../src/common/windows/MainWindow.cpp" line="933"/>
        <source>Qt远程桌面 - 客户端模式</source>
        <translation>Cross Remote Desktop - Client Mode</translation>
    </message>
    <message>
        <source>主机: %1
端口: %2
连接时间: %3</source>
        <translation type="vanished">Host: %1
Port: %2
Last Connected: %3</translation>
    </message>
</context>
<context>
    <name>ProtocolSession</name>
    <message>
        <location filename="../../src/client/session/ProtocolSession.cpp" line="36"/>
        <source>无法启动会话 - 未认证</source>
        <translation type="unfinished">Cannot start session - not authenticated</translation>
    </message>
    <message>
        <location filename="../../src/client/session/ProtocolSession.cpp" line="42"/>
        <source>解码管线未初始化</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../../src/main.cpp" line="326"/>
        <source>发生严重错误：%1</source>
        <translation>A critical error occurred: %1</translation>
    </message>
    <message>
        <location filename="../../src/main.cpp" line="331"/>
        <source>发生未知错误，应用程序将退出。</source>
        <translation>An unknown error occurred, the application will exit.</translation>
    </message>
</context>
<context>
    <name>ServerWorker</name>
    <message>
        <location filename="../../src/server/service/ServerWorker.cpp" line="109"/>
        <source>服务器启动失败</source>
        <translation>Server failed to start</translation>
    </message>
</context>
<context>
    <name>SessionManager</name>
    <message>
        <source>无法启动会话 - 未认证</source>
        <translation type="vanished">Cannot start session - not authenticated</translation>
    </message>
</context>
<context>
    <name>SettingsDialog</name>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="26"/>
        <source>应用程序设置</source>
        <translation>Application Settings</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="36"/>
        <source>/* ============================================================
 * SettingsDialog — Fluent Design (WinUI) 浅色主题
 * ============================================================ */

/* --- 窗口 --- */
QDialog#SettingsDialog {
    background-color: #f3f3f3;
    font-family: &quot;Segoe UI Variable&quot;, &quot;Segoe UI&quot;, &quot;Microsoft YaHei&quot;, sans-serif;
    font-size: 13px;
    color: #1a1a1a;
}

/* --- 侧栏区域 --- */
QWidget#sidebarWidget {
    background-color: #f3f3f3;
}

/* --- 侧栏导航列表 --- */
QListWidget#categoryListWidget {
    background-color: #f3f3f3;
    border: none;
    outline: none;
    font-size: 13px;
    padding: 6px 4px;
}

QListWidget#categoryListWidget::item {
    padding: 9px 12px;
    margin: 1px 4px;
    border-radius: 6px;
    border-left: 3px solid transparent;
    color: #424242;
    font-weight: 400;
}

QListWidget#categoryListWidget::item:selected {
    background-color: rgba(0, 0, 0, 0.06);
    border-left: 3px solid #0078d4;
    color: #1a1a1a;
    font-weight: 600;
}

QListWidget#categoryListWidget::item:hover:!selected {
    background-color: rgba(0, 0, 0, 0.03);
}

/* --- 内容区 --- */
QWidget#mainContentWidget {
    background-color: #fcfcfc;
    border-left: 1px solid #e5e5e5;
}

/* --- 分组框 → 白色卡片 --- */
QGroupBox {
    background-color: #ffffff;
    border: 1px solid #e8e8e8;
    border-radius: 8px;
    margin-top: 8px;
    padding: 18px 12px 12px 12px;
    font-weight: 600;
    font-size: 13px;
    color: #1a1a1a;
}

QGroupBox::title {
    subcontrol-origin: margin;
    left: 14px;
    padding: 0 6px 0 6px;
    color: #1a1a1a;
}

/* --- 标签 --- */
QLabel {
    color: #1a1a1a;
    font-weight: 400;
    font-size: 13px;
}

/* --- 下拉框 --- */
QComboBox {
    border: 1px solid #cfcfcf;
    border-radius: 4px;
    padding: 5px 10px;
    background-color: #ffffff;
    color: #1a1a1a;
    font-size: 13px;
    font-family: &quot;Segoe UI&quot;, sans-serif;
}

QComboBox:hover {
    border-color: #999999;
}

QComboBox:focus {
    border-color: #0078d4;
}

QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 24px;
    border: none;
    border-top-right-radius: 4px;
    border-bottom-right-radius: 4px;
}

QComboBox::down-arrow {
    image: url(:/icons/dropdown-arrow.svg);
    width: 12px;
    height: 12px;
    margin-right: 6px;
}

QComboBox QAbstractItemView {
    border: 1px solid #cfcfcf;
    border-radius: 4px;
    background-color: #ffffff;
    selection-background-color: rgba(0, 120, 212, 0.12);
    selection-color: #1a1a1a;
    outline: none;
}

/* --- 数字输入框 --- */
QSpinBox {
    border: 1px solid #cfcfcf;
    border-radius: 4px;
    padding: 8px 12px;
    background-color: #ffffff;
    color: #1a1a1a;
    font-size: 13px;
    font-family: &quot;Segoe UI&quot;, sans-serif;
}

QSpinBox:hover {
    border-color: #999999;
}

QSpinBox:focus {
    border-color: #0078d4;
}

QSpinBox::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
    width: 20px;
    border-left: 1px solid #cfcfcf;
    border-bottom: 1px solid #cfcfcf;
    border-top-right-radius: 4px;
}

QSpinBox::up-button:hover {
    background-color: #e8e8e8;
}

QSpinBox::up-arrow {
    image: url(:/icons/spin-up.svg);
    width: 8px;
    height: 8px;
}

QSpinBox::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
    width: 20px;
    border-left: 1px solid #cfcfcf;
    border-bottom-right-radius: 4px;
}

QSpinBox::down-button:hover {
    background-color: #e8e8e8;
}

QSpinBox::down-arrow {
    image: url(:/icons/spin-down.svg);
    width: 8px;
    height: 8px;
}

/* --- 文本框 --- */
QLineEdit {
    border: 1px solid #cfcfcf;
    border-radius: 4px;
    padding: 8px 12px;
    background-color: #ffffff;
    color: #1a1a1a;
    font-size: 13px;
    font-family: &quot;Segoe UI&quot;, sans-serif;
}

QLineEdit:hover {
    border-color: #999999;
}

QLineEdit:focus {
    border-color: #0078d4;
}

/* --- Toggle Switch (通过 QCheckBox::indicator 实现) --- */
QCheckBox {
    color: #1a1a1a;
    font-size: 13px;
    spacing: 10px;
}

QCheckBox::indicator {
    width: 40px;
    height: 20px;
    border-radius: 10px;
    background-color: #cccccc;
    border: none;
}

QCheckBox::indicator:checked {
    background-color: #0078d4;
}

QCheckBox::indicator:hover {
    background-color: #aaaaaa;
}

QCheckBox::indicator:checked:hover {
    background-color: #106ebe;
}

/* --- 开机自启 — 传统方框 checkbox --- */
QCheckBox#autoStartCheckBox {
    spacing: 8px;
}

QCheckBox#autoStartCheckBox::indicator {
    width: 16px;
    height: 16px;
    border-radius: 3px;
    background-color: #ffffff;
    border: 2px solid #cccccc;
}

QCheckBox#autoStartCheckBox::indicator:checked {
    background-color: #0078d4;
    border-color: #0078d4;
}

QCheckBox#autoStartCheckBox::indicator:hover {
    border-color: #999999;
}

QCheckBox#autoStartCheckBox::indicator:checked:hover {
    background-color: #106ebe;
    border-color: #106ebe;
}

/* --- 主要按钮（恢复默认值） --- */
QPushButton#restoreDefaultsBtn {
    background-color: #0078d4;
    color: #ffffff;
    border: none;
    border-radius: 4px;
    padding: 8px 20px;
    font-size: 13px;
    font-weight: 500;
    min-width: 100px;
    min-height: 32px;
}

QPushButton#restoreDefaultsBtn:hover {
    background-color: #106ebe;
}

QPushButton#restoreDefaultsBtn:pressed {
    background-color: #005a9e;
}

/* --- 辅助按钮（高级页 Enable Core Debug / Reset Rules） --- */
QPushButton#presetDebugBtn,
QPushButton#resetRulesBtn {
    background-color: #f5f5f5;
    border: 1px solid #d0d0d0;
    border-radius: 4px;
    padding: 5px 14px;
    font-size: 12px;
    font-weight: 400;
    color: #333333;
}

QPushButton#presetDebugBtn:hover,
QPushButton#resetRulesBtn:hover {
    background-color: #e8e8e8;
    border-color: #bcbcbc;
}


/* --- 多行文本框（日志规则） --- */
QTextEdit#logRulesTextEdit {
    border: 1px solid #cfcfcf;
    border-radius: 4px;
    padding: 8px 12px;
    background-color: #ffffff;
    color: #1a1a1a;
    font-family: &quot;Consolas&quot;, &quot;Cascadia Code&quot;, &quot;Courier New&quot;, monospace;
    font-size: 12px;
}

QTextEdit#logRulesTextEdit:focus {
    border-color: #0078d4;
}

/* --- 按钮栏区域 --- */
QWidget#buttonWidget {
    background-color: #f3f3f3;
    border-top: 1px solid #e5e5e5;
}

/* --- 滚动条 --- */
QScrollBar:vertical {
    width: 6px;
    background-color: transparent;
    margin: 0;
}

QScrollBar::handle:vertical {
    background-color: #c0c0c0;
    border-radius: 3px;
    min-height: 30px;
}

QScrollBar::handle:vertical:hover {
    background-color: #a0a0a0;
}

QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
    height: 0px;
}

QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical {
    background-color: transparent;
}

/* --- 堆叠页面透底 --- */
QStackedWidget#settingsStackedWidget {
    background-color: transparent;
}

/* --- 页面内各 QWidget 透底 --- */
QWidget#generalPage,
QWidget#serverPage,
QWidget#advancedPage {
    background-color: transparent;
}</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="461"/>
        <source>常规</source>
        <translation>General</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="471"/>
        <source>高级</source>
        <translation>Advanced</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="585"/>
        <source>开机自动启动</source>
        <translation>Start with system</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="534"/>
        <source>界面语言:</source>
        <translation>Interface Language:</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="548"/>
        <source>简体中文</source>
        <translation>Simplified Chinese</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="553"/>
        <source>英语</source>
        <translation>English</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="466"/>
        <source>通信</source>
        <translation>Communication</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="510"/>
        <source>语言</source>
        <translation>Language</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="564"/>
        <source>启动</source>
        <translation>Startup</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="615"/>
        <source>网络</source>
        <translation>Network</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="639"/>
        <source>监听端口:</source>
        <translation>Listen Port:</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="668"/>
        <source>认证</source>
        <translation>Authentication</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="680"/>
        <source>用户名:</source>
        <translation>Username:</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="693"/>
        <source>输入用户名</source>
        <translation>Enter Username</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="700"/>
        <source>密码:</source>
        <translation>Password:</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="716"/>
        <source>输入密码</source>
        <translation>Enter Password</translation>
    </message>
    <message>
        <source>👁</source>
        <translation type="vanished">[TODO]</translation>
    </message>
    <message>
        <source>显示/隐藏密码</source>
        <translation type="vanished">Show/Hide Password</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="746"/>
        <source>日志设置</source>
        <translation>Logging Settings</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="770"/>
        <source>日志级别:</source>
        <translation>Log Level:</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="784"/>
        <source>错误</source>
        <translation>Error</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="789"/>
        <source>警告</source>
        <translation>Warning</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="794"/>
        <source>信息</source>
        <translation>Info</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="799"/>
        <source>调试</source>
        <translation>Debug</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="807"/>
        <source>分类规则:</source>
        <translation>Category Rules:</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="814"/>
        <source>例如:\nlcApp.debug=true\n*.info=true\nqt.network.ssl.warning=false</source>
        <translation>[TODO]</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="912"/>
        <location filename="../../src/common/windows/SettingsDialog.cpp" line="305"/>
        <source>恢复默认值</source>
        <translation>Restore Defaults</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="829"/>
        <location filename="../../src/common/windows/SettingsDialog.cpp" line="303"/>
        <source>Enable Core Debug</source>
        <translation>Enable Core Debug</translation>
    </message>
    <message>
        <location filename="../../src/ui/settingsdialog.ui" line="836"/>
        <location filename="../../src/common/windows/SettingsDialog.cpp" line="304"/>
        <source>Reset Rules</source>
        <translation>Reset Rules</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/SettingsDialog.cpp" line="98"/>
        <source>中文</source>
        <translation>Chinese</translation>
    </message>
    <message>
        <location filename="../../src/common/windows/SettingsDialog.cpp" line="99"/>
        <source>English</source>
        <translation>English</translation>
    </message>
</context>
</TS>
