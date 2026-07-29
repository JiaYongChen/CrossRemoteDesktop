<#
.SYNOPSIS
    将中文→英文翻译记忆填入 resources/translations/en_US.ts。

.DESCRIPTION
    lupdate（update_translations 目标）重新生成 .ts 时会把英文译文重置为 unfinished。
    本脚本以内置的"中文源 → 英文译文"字典为翻译记忆，一键重填 en_US.ts。

    - 字典是人工维护的翻译记忆：新增 tr() 字符串后，请在下方字典补充对应英文，否则脚本报告"待补译"。
    - 仅填充仍为 unfinished 的条目；已有译文不覆盖。
    - 自动处理 XML 实体：普通条目的 & < > 自动转义；HTML 富文本条目按原文注入。

.EXAMPLE
    pwsh -File scripts/fill_en_translations.ps1
    # 或在 pwsh 会话中点源执行：
    . .\scripts\fill_en_translations.ps1

.NOTES
    必须用 pwsh 7+（UTF-8 默认）运行；Windows PowerShell 5.1 会按 GBK 误读中文。
#>
[CmdletBinding()]
param(
    # 不写回文件，仅报告匹配/待补译情况
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$tsPath = Join-Path $PSScriptRoot '..\resources\translations\en_US.ts'

if (-not (Test-Path $tsPath)) {
    Write-Error "找不到 $tsPath —— 请先运行 cmake --build build --target update_translations 生成 .ts"
}

# ============================================================
# 翻译记忆：中文源 → 英文译文（普通条目；& < > 会被自动转义）
# ============================================================
$dict = [ordered]@{
    '远程桌面' = 'Remote Desktop'
    '仅查看' = 'View Only'
    '连接' = 'Connect'
    '修改参数' = 'Edit Parameters'
    '删除记录' = 'Delete Record'
    '极域桌面连接' = 'UltraDesktop Connection'
    '连接信息' = 'Connection Information'
    '服务器信息' = 'Server Information'
    '主机名:' = 'Hostname:'
    '可选的友好名称（如&quot;办公室电脑&quot;）' = 'Optional friendly name (e.g. "Office PC")'
    '主机地址:' = 'Host Address:'
    'IP 或主机名，可含端口如 192.168.1.100:5921' = 'IP or hostname, optionally with port, e.g. 192.168.1.100:5921'
    '身份验证' = 'Authentication'
    '用户名:' = 'Username:'
    '输入用户名（可选）' = 'Enter username (optional)'
    '密码:' = 'Password:'
    '输入密码（可选）' = 'Enter password (optional)'
    '显示 &amp;&amp; 功能' = 'Display &amp;&amp; Features'
    '显示设置' = 'Display Settings'
    '颜色深度:' = 'Color Depth:'
    '16位 (65K 颜色)' = '16-bit (65K colors)'
    '24位 (16M 颜色)' = '24-bit (16M colors)'
    '32位 (真彩色)' = '32-bit (True Color)'
    '显示模式:' = 'Display Mode:'
    '全屏模式' = 'Fullscreen Mode'
    '×' = '×'
    '图像质量' = 'Image Quality'
    '功能选项' = 'Feature Options'
    '启用剪贴板同步' = 'Enable Clipboard Sync'
    '显示远程光标' = 'Show Remote Cursor'
    '仅查看模式（禁用输入）' = 'View-only Mode (disable input)'
    '网络' = 'Network'
    '网络设置' = 'Network Settings'
    '连接超时:' = 'Connection Timeout:'
    ' 秒' = ' s'
    '自动重连' = 'Auto Reconnect'
    '重连间隔:' = 'Reconnect Interval:'
    '请输入有效的主机地址' = 'Please enter a valid host address'
    '主机地址不能包含空格' = 'Host address cannot contain spaces'
    '端口号必须在1-65535之间' = 'Port must be between 1 and 65535'
    '验证错误' = 'Validation Error'
    '%1 - %2' = '%1 - %2'
    '正在连接...' = 'Connecting...'
    '已连接' = 'Connected'
    '已认证' = 'Authenticated'
    '正在重连...' = 'Reconnecting...'
    '正在断开连接...' = 'Disconnecting...'
    '未连接' = 'Not Connected'
    '连接错误' = 'Connection Error'
    '认证失败' = 'Authentication Failed'
    '%1 [仅查看]' = '%1 [View Only]'
    '连接已断开' = 'Connection Disconnected'
    '与远程主机 %1 的连接已断开。' = 'The connection to remote host %1 has been disconnected.'
    '服务器' = 'Server'
    '窗口即将关闭。' = 'The window will close.'
    '用户名: %1' = 'Username: %1'
    '重试' = 'Retry'
    '取消' = 'Cancel'
    '认证失败：用户名无效' = 'Authentication failed: invalid username'
    '认证失败：密码错误' = 'Authentication failed: incorrect password'
    '认证失败：尝试次数过多，请稍后重试' = 'Authentication failed: too many attempts, please try again later'
    'searchBox' = 'searchBox'
    '搜索历史连接...' = 'Search connection history...'
    'emptyStateLabel' = 'emptyStateLabel'
    '暂无连接历史' = 'No connection history'
    '确认删除' = 'Confirm Deletion'
    '确定删除此连接记录？' = 'Delete this connection record?'
    '无匹配的连接记录' = 'No matching connection records'
    '全屏切换' = 'Toggle Fullscreen'
    '断开连接' = 'Disconnect'
    '仅查看切换' = 'Toggle View Only'
    '退出仅查看' = 'Exit View Only'
    '极域桌面' = 'UltraDesktop'
    '退出' = 'Exit'
    '恢复(&amp;R)' = '&amp;Restore'
    '连接：未连接' = 'Connection: Not Connected'
    '服务器：已停止' = 'Server: Stopped'
    '就绪' = 'Ready'
    '服务器状态' = 'Server Status'
    '服务器已经在运行中。' = 'The server is already running.'
    '需要辅助功能权限' = 'Accessibility Permission Required'
    '关于极域桌面' = 'About UltraDesktop'
    '关于' = 'About'
    '开源许可' = 'Open Source Licenses'
    'CPU: --% | 内存: %1 MB' = 'CPU: --% | Memory: %1 MB'
    'CPU: %1% | 内存: %2 MB' = 'CPU: %1% | Memory: %2 MB'
    '内存: %1 MB' = 'Memory: %1 MB'
    '客户端已连接: %1' = 'Client connected: %1'
    '客户端已断开: %1' = 'Client disconnected: %1'
    '客户端已认证: %1' = 'Client authenticated: %1'
    'CPU: 0% | 内存: 0MB' = 'CPU: 0% | Memory: 0MB'
    '菜单' = 'Menu'
    '新建连接' = 'New Connection'
    '设置' = 'Settings'
    '切换主题' = 'Toggle Theme'
    '无法启动会话 - 未认证' = 'Cannot start session - not authenticated'
    '解码管线未初始化' = 'Decode pipeline not initialized'
    '发生严重错误：%1' = 'A critical error occurred: %1'
    '发生未知错误，应用程序将退出。' = 'An unknown error occurred. The application will exit.'
    '应用程序设置' = 'Application Settings'
    '常规' = 'General'
    '通信' = 'Communication'
    '高级' = 'Advanced'
    '语言' = 'Language'
    '界面语言:' = 'Interface Language:'
    '简体中文' = 'Simplified Chinese'
    '英语' = 'English'
    '启动' = 'Startup'
    '开机自动启动' = 'Start automatically on boot'
    '关闭行为' = 'Close Behavior'
    '关闭时隐藏到系统托盘' = 'Hide to system tray on close'
    '监听端口:' = 'Listen Port:'
    '认证' = 'Authentication'
    '当为空时跳过认证' = 'Skip authentication when empty'
    '日志设置' = 'Logging Settings'
    '日志级别:' = 'Log Level:'
    '错误' = 'Error'
    '警告' = 'Warning'
    '信息' = 'Info'
    '调试' = 'Debug'
    '分类规则:' = 'Category Rules:'
    '例如:\nlcApp.debug=true\n*.info=true\nqt.network.ssl.warning=false' = 'Example:\nlcApp.debug=true\n*.info=true\nqt.network.ssl.warning=false'
    '启用核心调试' = 'Enable Core Debug'
    '重置规则' = 'Reset Rules'
    '恢复默认值' = 'Restore Defaults'
    '中文' = 'Chinese'
    'English' = 'English'
    '认证配置不完整' = 'Incomplete Authentication Configuration'
    '用户名和密码必须同时填写或同时留空以跳过认证。' = 'Username and password must both be filled in or both left empty to skip authentication.'
    'TCP服务器未初始化' = 'TCP server not initialized'
    '服务器启动失败' = 'Failed to start server'
}

# ============================================================
# HTML 富文本条目：中文源 → 英文译文（均已 XML 实体化，按原文注入不再转义）
# ============================================================
$html = [ordered]@{}
$html['&lt;p&gt;极域桌面需要&lt;b&gt;辅助功能权限&lt;/b&gt;才能模拟鼠标和键盘输入。&lt;/p&gt;&lt;p&gt;请按照以下步骤授予权限：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;系统偏好设置&lt;/b&gt;&lt;/li&gt;&lt;li&gt;选择&lt;b&gt;安全性与隐私&lt;/b&gt;&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;隐私&lt;/b&gt;标签&lt;/li&gt;&lt;li&gt;在左侧列表中选择&lt;b&gt;辅助功能&lt;/b&gt;&lt;/li&gt;&lt;li&gt;点击左下角的锁图标解锁&lt;/li&gt;&lt;li&gt;在右侧列表中勾选&lt;b&gt;UltraDesktop&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;授予权限后，请重启应用程序。&lt;/p&gt;'] = '&lt;p&gt;UltraDesktop requires &lt;b&gt;Accessibility permission&lt;/b&gt; to simulate mouse and keyboard input.&lt;/p&gt;&lt;p&gt;Please follow these steps to grant permission:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Open &lt;b&gt;System Preferences&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Select &lt;b&gt;Security &amp;amp; Privacy&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Click the &lt;b&gt;Privacy&lt;/b&gt; tab&lt;/li&gt;&lt;li&gt;Select &lt;b&gt;Accessibility&lt;/b&gt; in the left list&lt;/li&gt;&lt;li&gt;Click the lock icon at the bottom left to unlock&lt;/li&gt;&lt;li&gt;Check &lt;b&gt;UltraDesktop&lt;/b&gt; in the right list&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;After granting permission, please restart the application.&lt;/p&gt;'
$html['&lt;h2&gt;%1 %2&lt;/h2&gt;&lt;p&gt;基于 Qt %3 构建的跨平台远程桌面应用程序。&lt;/p&gt;&lt;p&gt;支持 Windows、macOS 和 Linux 系统之间的远程连接与控制。&lt;/p&gt;&lt;hr&gt;&lt;p&gt;本软件遵循 &lt;b&gt;MIT License&lt;/b&gt; 开源发布。&lt;/p&gt;&lt;p&gt;详情请见程序目录下的 LICENSE 文件。&lt;/p&gt;'] = '&lt;h2&gt;%1 %2&lt;/h2&gt;&lt;p&gt;A cross-platform remote desktop application built on Qt %3.&lt;/p&gt;&lt;p&gt;Supports remote connection and control between Windows, macOS, and Linux systems.&lt;/p&gt;&lt;hr&gt;&lt;p&gt;This software is released under the &lt;b&gt;MIT License&lt;/b&gt;.&lt;/p&gt;&lt;p&gt;See the LICENSE file in the program directory for details.&lt;/p&gt;'
$html['&lt;h3&gt;第三方开源库许可声明&lt;/h3&gt;&lt;h4&gt;Qt %1 &amp;mdash; LGPLv3&lt;/h4&gt;&lt;p&gt;Copyright &amp;copy; The Qt Company Ltd. and other contributors.&lt;/p&gt;&lt;p&gt;Qt 采用 GNU Lesser General Public License v3 (LGPLv3) 许可。本应用程序通过动态链接方式使用 Qt 库，符合 LGPLv3 的闭源分发条件。Qt 源代码可从 &lt;a href=&apos;https://download.qt.io&apos;&gt;https://download.qt.io&lt;/a&gt; 获取。&lt;/p&gt;&lt;p&gt;LGPLv3 全文：&lt;a href=&apos;https://www.gnu.org/licenses/lgpl-3.0.html&apos;&gt;https://www.gnu.org/licenses/lgpl-3.0.html&lt;/a&gt;&lt;/p&gt;&lt;h4&gt;OpenSSL 3.x &amp;mdash; Apache License 2.0&lt;/h4&gt;&lt;p&gt;Copyright &amp;copy; The OpenSSL Project Authors. All Rights Reserved.&lt;/p&gt;&lt;p&gt;Licensed under the Apache License 2.0 (the &quot;License&quot;); you may not use this file except in compliance with the License. You may obtain a copy of the License at &lt;a href=&apos;https://www.apache.org/licenses/LICENSE-2.0&apos;&gt;https://www.apache.org/licenses/LICENSE-2.0&lt;/a&gt;&lt;/p&gt;&lt;p&gt;本产品包含由 OpenSSL Project 开发的、用于 OpenSSL Toolkit 的软件(&lt;a href=&apos;https://www.openssl.org/&apos;&gt;https://www.openssl.org/&lt;/a&gt;)。&lt;/p&gt;&lt;h4&gt;libjpeg-turbo &amp;mdash; BSD 3-Clause&lt;/h4&gt;&lt;p&gt;Copyright &amp;copy; 2009-2026 D. R. Commander. All Rights Reserved.&lt;br&gt;Copyright &amp;copy; 2015 Viktor Szathm&amp;aacute;ry. All Rights Reserved.&lt;/p&gt;&lt;p&gt;在满足下列条件的前提下，允许以源代码和二进制形式重新分发和使用（无论是否修改）：&lt;/p&gt;&lt;ul&gt;&lt;li&gt;源代码的再分发必须保留上述版权声明、本条件列表以及下述免责声明。&lt;/li&gt;&lt;li&gt;二进制形式的再分发必须在文档和/或随分发提供的其他材料中复现上述版权声明、本条件列表以及下述免责声明。&lt;/li&gt;&lt;li&gt;未经事先书面许可，不得使用 libjpeg-turbo 项目或其贡献者的名称来背书或推广从本软件衍生的产品。&lt;/li&gt;&lt;/ul&gt;&lt;p&gt;THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS &quot;AS IS&quot; ...&lt;/p&gt;&lt;h4&gt;nvJPEG / CUDA Runtime &amp;mdash; NVIDIA 专有许可&lt;/h4&gt;&lt;p&gt;Copyright &amp;copy; NVIDIA Corporation. All Rights Reserved.&lt;/p&gt;&lt;p&gt;nvJPEG 和 CUDA Runtime 库为 NVIDIA Corporation 的专有软件，受 NVIDIA CUDA Toolkit 最终用户许可协议 (EULA) 约束。仅可在搭载 NVIDIA GPU 的系统上使用和分发。详情请参阅：&lt;a href=&apos;https://docs.nvidia.com/cuda/eula/index.html&apos;&gt;https://docs.nvidia.com/cuda/eula/index.html&lt;/a&gt;&lt;/p&gt;'] = '&lt;h3&gt;Third-Party Open Source Library Licenses&lt;/h3&gt;&lt;h4&gt;Qt %1 &amp;mdash; LGPLv3&lt;/h4&gt;&lt;p&gt;Copyright &amp;copy; The Qt Company Ltd. and other contributors.&lt;/p&gt;&lt;p&gt;Qt is licensed under the GNU Lesser General Public License v3 (LGPLv3). This application dynamically links the Qt libraries, complying with the LGPLv3 terms for closed-source distribution. Qt source code is available from &lt;a href=&apos;https://download.qt.io&apos;&gt;https://download.qt.io&lt;/a&gt;.&lt;/p&gt;&lt;p&gt;Full LGPLv3 text: &lt;a href=&apos;https://www.gnu.org/licenses/lgpl-3.0.html&apos;&gt;https://www.gnu.org/licenses/lgpl-3.0.html&lt;/a&gt;&lt;/p&gt;&lt;h4&gt;OpenSSL 3.x &amp;mdash; Apache License 2.0&lt;/h4&gt;&lt;p&gt;Copyright &amp;copy; The OpenSSL Project Authors. All Rights Reserved.&lt;/p&gt;&lt;p&gt;Licensed under the Apache License 2.0 (the &quot;License&quot;); you may not use this file except in compliance with the License. You may obtain a copy of the License at &lt;a href=&apos;https://www.apache.org/licenses/LICENSE-2.0&apos;&gt;https://www.apache.org/licenses/LICENSE-2.0&lt;/a&gt;&lt;/p&gt;&lt;p&gt;This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit (&lt;a href=&apos;https://www.openssl.org/&apos;&gt;https://www.openssl.org/&lt;/a&gt;).&lt;/p&gt;&lt;h4&gt;libjpeg-turbo &amp;mdash; BSD 3-Clause&lt;/h4&gt;&lt;p&gt;Copyright &amp;copy; 2009-2026 D. R. Commander. All Rights Reserved.&lt;br&gt;Copyright &amp;copy; 2015 Viktor Szathm&amp;aacute;ry. All Rights Reserved.&lt;/p&gt;&lt;p&gt;Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:&lt;/p&gt;&lt;ul&gt;&lt;li&gt;Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.&lt;/li&gt;&lt;li&gt;Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.&lt;/li&gt;&lt;li&gt;Neither the name of the libjpeg-turbo Project nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.&lt;/li&gt;&lt;/ul&gt;&lt;p&gt;THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS &quot;AS IS&quot; ...&lt;/p&gt;&lt;h4&gt;nvJPEG / CUDA Runtime &amp;mdash; NVIDIA Proprietary License&lt;/h4&gt;&lt;p&gt;Copyright &amp;copy; NVIDIA Corporation. All Rights Reserved.&lt;/p&gt;&lt;p&gt;The nvJPEG and CUDA Runtime libraries are proprietary software of NVIDIA Corporation, subject to the NVIDIA CUDA Toolkit End User License Agreement (EULA). They may only be used and distributed on systems with NVIDIA GPUs. See: &lt;a href=&apos;https://docs.nvidia.com/cuda/eula/index.html&apos;&gt;https://docs.nvidia.com/cuda/eula/index.html&lt;/a&gt;&lt;/p&gt;'

# ============================================================
# 执行填充
# ============================================================
function Escape-Xml([string]$s) {
    return $s.Replace('&', '&amp;').Replace('<', '&lt;').Replace('>', '&gt;')
}

# 合并翻译记忆：HTML 条目按原文，普通条目转义后注入
$all = [ordered]@{}
foreach ($k in $dict.Keys) { $all[$k] = (Escape-Xml $dict[$k]) }
foreach ($k in $html.Keys) { $all[$k] = $html[$k] }

$content = Get-Content $tsPath -Raw -Encoding UTF8

# 文件中实际存在的全部源文本（用于判断字典条目是否冗余）
$inFile = @{}
foreach ($m in [regex]::Matches($content, '(?s)<source>(.*?)</source>')) { $inFile[$m.Groups[1].Value] = $true }

$filled = 0
foreach ($src in $all.Keys) {
    $en = $all[$src]
    # 仅替换 unfinished 条目；显式重写完整 <translation> 闭合标签（避免半截标签 bug）；
    # 译文中可能含 $，用 .Replace('$$') 转义为正则替换的字面 $。
    $pattern = '(<source>' + [regex]::Escape($src) + '</source>\s*)<translation type="unfinished"></translation>'
    $before = $content
    $content = [regex]::Replace($content, $pattern, ('${1}<translation>' + $en.Replace('$', '$$') + '</translation>'))
    if ($content -ne $before) { $filled++ }
}

# 报告 .ts 中仍为 unfinished 且字典未覆盖的源（待补译）
$unmatched = @()
foreach ($m in [regex]::Matches($content, '(?s)<source>(.*?)</source>\s*<translation type="unfinished"')) {
    $unmatched += $m.Groups[1].Value
}

if (-not $DryRun) {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText((Resolve-Path $tsPath), $content, $utf8NoBom)
}

# ============================================================
# 结果报告
# ============================================================
Write-Host ("en_US.ts 填充完成（{0}）" -f $(if ($DryRun) { 'DryRun，未写回' } else { '已写回' }))
Write-Host ("  本次填充: {0} 条" -f $filled)
Write-Host ("  字典条目: {0}（普通 {1} + HTML {2}）" -f $all.Count, $dict.Count, $html.Count)
$remaining = ([regex]::Matches($content, 'type="unfinished"')).Count
Write-Host ("  剩余 unfinished: {0}" -f $remaining)

if ($unmatched.Count -gt 0) {
    Write-Host ""
    Write-Host ("待补译（字典未覆盖，请在脚本字典中补充英文，截断显示）:") -ForegroundColor Yellow
    $unmatched | Select-Object -Unique | ForEach-Object {
        $s = if ($_.Length -gt 50) { $_.Substring(0, 50) + '...' } else { $_ }
        Write-Host ("  [$s]")
    }
}

# 字典中源文本已不在 .ts 的条目（tr() 已删除或改动，提示清理字典）
$unused = @($all.Keys | Where-Object { -not $inFile.ContainsKey($_) })
if ($unused.Count -gt 0) {
    Write-Host ""
    Write-Host ("字典冗余（源文本已不在 .ts，可考虑从字典移除，截断显示）:") -ForegroundColor DarkYellow
    $unused | ForEach-Object {
        $s = if ($_.Length -gt 50) { $_.Substring(0, 50) + '...' } else { $_ }
        Write-Host ("  [$s]")
    }
}
