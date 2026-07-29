<#
.SYNOPSIS
    全自动翻译：调用 Google Translate 免费端点，把 en_US.ts 中 unfinished 的中文源自动译为英文。

.DESCRIPTION
    扫描 resources/translations/en_US.ts 中 type="unfinished" 的条目，批量送 Google 免费翻译端点
    （translate.googleapis.com，无需 key），将返回的英文写回。
    API 失败或单条翻译失败时，回退到内置字典（与 fill_en_translations.ps1 同源）补译，最大化成功率。

.EXAMPLE
    pwsh -File scripts/auto_translate_en.ps1
    pwsh -File scripts/auto_translate_en.ps1 -DryRun     # 仅预览，不写回

.NOTES
    必须用 pwsh 7+（UTF-8 默认）运行；Windows PowerShell 5.1 会按 GBK 误读中文。
    免费端点非官方接口，可能限流/被墙/返回异常——已内置字典回退兜底，但不能保证 100% 在线翻译成功。
    若网络不通，可改用离线方案：pwsh -File scripts/fill_en_translations.ps1（纯字典）。
#>
[CmdletBinding()]
param([switch]$DryRun)

$ErrorActionPreference = 'Stop'
$tsPath = Join-Path $PSScriptRoot '..\resources\translations\en_US.ts'
if (-not (Test-Path $tsPath)) {
    Write-Error "找不到 $tsPath —— 请先运行 cmake --build build --target update_translations 生成 .ts"
}

# ---- 内置回退字典（精简常用条目，完整版见 fill_en_translations.ps1）----
$fallback = @{
    '远程桌面' = 'Remote Desktop'; '连接' = 'Connect'; '设置' = 'Settings'
    '退出' = 'Exit'; '取消' = 'Cancel'; '重试' = 'Retry'; '关于' = 'About'
    '菜单' = 'Menu'; '新建连接' = 'New Connection'; '断开连接' = 'Disconnect'
    '已连接' = 'Connected'; '未连接' = 'Not Connected'; '认证失败' = 'Authentication Failed'
    '正在连接...' = 'Connecting...'; '正在重连...' = 'Reconnecting...'
    '全屏切换' = 'Toggle Fullscreen'; '仅查看' = 'View Only'; '就绪' = 'Ready'
}
function Escape-Xml([string]$s) { $s.Replace('&', '&amp;').Replace('<', '&lt;').Replace('>', '&gt;') }

# ---- 收集 unfinished 条目 ----
$content = Get-Content $tsPath -Raw -Encoding UTF8
$msgs = [regex]::Matches($content, '(?s)<message>.*?</message>')
$pending = [ordered]@{}
foreach ($m in $msgs) {
    $t = $m.Value
    if ($t -match 'type="unfinished"') {
        $src = [regex]::Match($t, '(?s)<source>(.*?)</source>').Groups[1].Value
        if (-not $pending.Contains($src)) { $pending[$src] = $true }
    }
}
Write-Host ("待翻译条目: {0}" -f $pending.Count)
if ($pending.Count -eq 0) { Write-Host "无需翻译，en_US.ts 已全部完成。"; return }

# ---- 调用 Google 免费端点批量翻译 ----
function Invoke-GoogleTranslate([string[]]$texts) {
    # GET 单条端点（实测可用；batchTranslate 返回 404）。逐条请求，规避限流加小延时。
    $out = @()
    foreach ($t in $texts) {
        $url = 'https://translate.googleapis.com/translate_a/single?client=gtx&sl=zh-CN&tl=en&dt=t&q=' +
               [System.Net.WebUtility]::UrlEncode($t)
        $resp = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 25
        $tr = ((($resp.Content | ConvertFrom-Json)[0] | ForEach-Object { $_[0] }) -join '')
        # UI 串首字母大写（Google 常返回全小写）
        if ($tr.Length -gt 0) { $tr = [char]::ToUpperInvariant($tr[0]) + $tr.Substring(1) }
        $out += $tr
        Start-Sleep -Milliseconds 150
    }
    return ,$out
}

$sourceList = @($pending.Keys)
$results = @{}
$apiOk = $true
try {
    Write-Host "调用 Google 翻译端点..."
    $translated = Invoke-GoogleTranslate $sourceList
    if ($translated.Count -eq $sourceList.Count) {
        for ($i = 0; $i -lt $sourceList.Count; $i++) { $results[$sourceList[$i]] = $translated[$i] }
        Write-Host ("  API 翻译成功: {0} 条" -f $translated.Count)
    } else { $apiOk = $false }
} catch {
    $apiOk = $false
    Write-Warning ("  API 调用失败: {0}" -f $_.Exception.Message)
}

# ---- API 失败/缺漏的条目回退到字典 ----
$usedFallback = @()
foreach ($src in $sourceList) {
    if (-not $results.ContainsKey($src)) {
        if ($fallback.ContainsKey($src)) {
            $results[$src] = Escape-Xml $fallback[$src]
            $usedFallback += $src
        }
    }
}
if ($usedFallback.Count -gt 0) { Write-Host ("  字典回退: {0} 条" -f $usedFallback.Count) }

# ---- 写回 .ts ----
$filled = 0
$failed = @()
foreach ($src in $sourceList) {
    if (-not $results.ContainsKey($src)) { $failed += $src; continue }
    $en = $results[$src] -replace '\s+$', ''
    if ([string]::IsNullOrWhiteSpace($en)) { $failed += $src; continue }
    $pattern = '(<source>' + [regex]::Escape($src) + '</source>\s*)<translation type="unfinished"></translation>'
    $before = $content
    $content = [regex]::Replace($content, $pattern, ('${1}<translation>' + $en.Replace('$', '$$') + '</translation>'))
    if ($content -ne $before) { $filled++ }
}
if (-not $DryRun) {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText((Resolve-Path $tsPath), $content, $utf8NoBom)
}

# ---- 报告 ----
Write-Host ("自动翻译完成（{0}）" -f $(if ($DryRun) { 'DryRun 未写回' } else { '已写回' }))
Write-Host ("  成功: {0} 条（API: {1}）" -f $filled, $(if ($apiOk) { '在线' } else { '不可用，走回退' }))
$remaining = ([regex]::Matches($content, 'type="unfinished"')).Count
Write-Host ("  剩余 unfinished: {0}" -f $remaining)
if ($failed.Count -gt 0) {
    Write-Host ("未翻译成功（API 与字典均未覆盖，需人工，截断显示）:") -ForegroundColor Yellow
    $failed | ForEach-Object {
        $s = if ($_.Length -gt 50) { $_.Substring(0, 50) + '...' } else { $_ }
        Write-Host ("  [$s]")
    }
}
