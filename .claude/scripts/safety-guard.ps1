# 命令安全检查 Hook — 低风险自动放行，其余 AI 判断或弹窗
# PreToolUse hook for Bash|PowerShell
$ErrorActionPreference = "Stop"

# 使用 $input 自动变量读取管道数据（hook 框架通过 stdin 管道传入 JSON）
$rawInput = $input | Out-String
if (-not $rawInput -or $rawInput.Trim().Length -eq 0) {
    # 无输入，不做决策
    exit 0
}

try {
    $hookData = $rawInput | ConvertFrom-Json
} catch {
    return
}

$command = $hookData.tool_input.command
if (-not $command) {
    return
}

# ── 第一层：静态白名单 ───────────────────────────────
# 将复合命令按 && || ; 连接符与管道 | 拆为独立段，逐段判定。
# 仅当「每一段都低风险」且「无任一段命中危险模式」时才静态放行，
# 否则整体交由第二层 AI 判断——杜绝「良性首段 && 危险后段」绕过。
$segments = @($command -split '\s*(?:&&|\|\||[;&|])\s*' | Where-Object { $_.Trim().Length -gt 0 })
if ($segments.Count -eq 0) { return }

# 通用只读/低风险主命令
$lowRiskPatterns = @(
    # 读操作
    '^get-content$', '^get-childitem$', '^ls$', '^dir$', '^cat$',
    '^select-string$', '^test-path$', '^where(\.exe)?$', '^measure-object$',
    '^select-object$', '^format-', '^out-', '^write-output$', '^echo$', '^type$',
    '^findstr$', '^tasklist$',
    # 构建/测试
    '^cmake$', '^ctest$', '^msbuild$',
    # 文件创建
    '^new-item$', '^set-content$', '^mkdir$',
    # 环境变量
    '^set$', '^export$', '^printenv$'
)

# git 只读子命令
$gitReadOnly = @('status', 'log', 'diff', 'show', 'rev-parse', 'remote', 'stash', 'branch', 'tag', 'config', 'ls-files', 'ls-tree', 'describe', 'blame', 'grep', 'rev-list', 'whatchanged', 'shortlog', 'reflog', 'cherry', 'format-patch', 'archive', 'bundle', 'verify-commit', 'verify-tag', 'check-ref-format', 'check-ignore', 'check-attr', 'check-mailmap', 'column', 'credential', 'difftool', 'fetch', 'help', 'instaweb', 'mergetool', 'notes', 'range-diff', 'replace', 'request-pull', 'show-branch')
# git 低风险写子命令（本地、可逆、非强制；push/reset/clean 等仍交 AI 层）
$gitSafeWrite = @('add', 'commit')

# 危险操作模式（对每一段独立检查）
$dangerousPatterns = @(
    '\brm\s+.*\b-rf?\b|\brmdir\b|\bremove-item\b.*\b-recurse\b',
    '\bcurl\b|\bwget\b|\binvoke-webrequest\b|\binvoke-restmethod\b',
    '\btaskkill\b|\bstop-process\b|\bkill\b|\bkillall\b',
    '\breg\s+add\b|\breg\s+delete\b|\bicacls\b|\bset-service\b',
    '\bgit\s+push\b.*-f|\bgit\s+reset\s+--hard\b|\bgit\s+clean\b',
    '\brunas\b|\bsudo\b|\bstart-process\b',
    '\bdel\s+/[sfq]\b|\brd\s+/[sq]\b',
    '\binvoke-expression\b|\biex\b',
    '\bset-executionpolicy\b|\bunblock-file\b'
)

# 单段低风险判定
function Test-SegmentLowRisk {
    param([string]$Segment)
    $seg = $Segment.Trim()
    $tokens = $seg -split '\s+'
    if ($tokens.Count -eq 0) { return $false }

    # 环境变量赋值（含 PowerShell 的 $env:NAME = ... 形式）
    if ($seg -match '^\$(?:env:)?\w+\s*=' -or $seg -match '^[A-Z_]+\s*=') { return $true }

    $main = ($tokens[0] -replace '^.*[/\\]', '').ToLower()

    # git 细分：只读 + 安全写放行，其余（push/reset/...）交 AI 层
    if ($main -eq 'git') {
        if ($tokens.Count -lt 2) { return $false }
        $sub = $tokens[1].ToLower()
        return ($sub -in $gitReadOnly) -or ($sub -in $gitSafeWrite)
    }

    foreach ($p in $lowRiskPatterns) {
        if ($main -match $p) { return $true }
    }
    return $false
}

# 任一段命中危险模式 → 整体不放行
$isDangerous = $false
foreach ($seg in $segments) {
    foreach ($p in $dangerousPatterns) {
        if ($seg -match $p) { $isDangerous = $true; break }
    }
    if ($isDangerous) { break }
}

# 所有段均低风险且无危险 → 静态放行
$isLowRisk = -not $isDangerous
$reason = ""
if ($isLowRisk) {
    foreach ($seg in $segments) {
        if (-not (Test-SegmentLowRisk $seg)) { $isLowRisk = $false; break }
    }
    if ($isLowRisk) { $reason = "静态规则: $($segments.Count) 段全部低风险" }
}

if ($isLowRisk) {
    Write-Output (ConvertTo-Json -Compress -Depth 3 @{
        hookSpecificOutput = @{
            hookEventName = "PreToolUse"
            permissionDecision = "allow"
            permissionDecisionReason = $reason
        }
    })
    exit 0
}

# ── 第二层：AI 语义判断 ──────────────────────────────
# 统一「弹窗并说明原因」出口：返回 ask 决策，让 Claude Code 弹窗展示理由、交由用户决定
function Write-AskDecision {
    param([string]$Reason)
    Write-Output (ConvertTo-Json -Compress -Depth 3 @{
        hookSpecificOutput = @{
            hookEventName = "PreToolUse"
            permissionDecision = "ask"
            permissionDecisionReason = $Reason
        }
    })
}

# 凭据解析：优先 ANTHROPIC_API_KEY（官方 x-api-key），其次 ANTHROPIC_AUTH_TOKEN（代理 Bearer）
$apiKey = $env:ANTHROPIC_API_KEY
$authToken = $env:ANTHROPIC_AUTH_TOKEN
if (-not $apiKey -and -not $authToken) {
    # 无可用凭据，无法 AI 判定 → 弹窗并说明原因
    [Console]::Error.WriteLine("safety-guard: 未发现 ANTHROPIC_API_KEY/ANTHROPIC_AUTH_TOKEN，跳过 AI 层")
    Write-AskDecision "未配置 ANTHROPIC 凭据，无法进行 AI 判定，请人工确认"
    exit 0
}

$headers = @{
    "anthropic-version" = "2023-06-01"
    "content-type" = "application/json"
}
if ($apiKey) {
    $headers["x-api-key"] = $apiKey
} else {
    $headers["Authorization"] = "Bearer $authToken"
}

# 端点解析：优先 ANTHROPIC_BASE_URL（代理端点），回退官方地址
$baseUrl = $env:ANTHROPIC_BASE_URL
if ($baseUrl) {
    $apiUri = $baseUrl.TrimEnd('/') + "/v1/messages"
} else {
    $apiUri = "https://api.anthropic.com/v1/messages"
}

# 模型解析：ANTHROPIC_DEFAULT_HAIKU_MODEL 覆盖 > 默认 claude-haiku
$model = $env:ANTHROPIC_DEFAULT_HAIKU_MODEL
if (-not $model) {
    $model = "claude-haiku-4-5-20251001"
}
# 剥离 Claude Code 上下文窗口标注（如 model[1m]），API 只接受纯模型 ID
$model = $model -replace '\s*\[.*\]$',''

$prompt = @"
你是命令安全分类器。判断以下命令是否可以安全执行。
只输出一个 JSON: {"allow":true|false,"reason":"简短中文理由"}

命令: $command
"@

$body = @{
    model = $model
    max_tokens = 1024
    messages = @(
        @{
            role = "user"
            content = $prompt
        }
    )
} | ConvertTo-Json -Depth 3

# 兜底弹窗理由：各失败分支会覆写为更具体的原因
$askReason = "AI 层未能给出明确结论，请人工确认"
try {
    $response = Invoke-RestMethod `
        -Uri $apiUri `
        -Method Post `
        -Headers $headers `
        -Body ([System.Text.Encoding]::UTF8.GetBytes($body)) `
        -TimeoutSec 30

    # 思考模型（如 qwen3.8-max-preview）的可见答复在 type=text 块，content[0] 可能是 thinking 块
    $textBlock = $response.content | Where-Object { $_.type -eq 'text' } | Select-Object -Last 1
    $aiText = if ($textBlock) { $textBlock.text.Trim() } else { "" }
    # 提取 JSON（AI 可能包裹在 markdown 代码块中）
    if ($aiText -match '\{[\s\S]*\}') {
        $aiJson = $Matches[0]
        try {
            $aiResult = $aiJson | ConvertFrom-Json
            if ($aiResult.allow -eq $true) {
                Write-Output (ConvertTo-Json -Compress -Depth 3 @{
                    hookSpecificOutput = @{
                        hookEventName = "PreToolUse"
                        permissionDecision = "allow"
                        permissionDecisionReason = "AI判断: $($aiResult.reason)"
                    }
                })
                exit 0
            } else {
                # AI 明确判定不安全 → 弹窗并展示 AI 给出的风险理由
                $askReason = "AI 风险提示: $($aiResult.reason)"
            }
        } catch {
            $askReason = "AI 响应 JSON 解析失败，请人工确认"
        }
    } else {
        $askReason = "AI 响应未包含可解析结论，请人工确认"
    }
} catch {
    # 超时/网络失败 → 弹窗并说明具体异常
    $askReason = "AI 分类调用失败(超时/网络)，请人工确认: $($_.Exception.Message)"
    [Console]::Error.WriteLine("safety-guard: $askReason")
}

# 未获 AI 放行：弹窗并携带具体原因，交由用户决定
Write-AskDecision $askReason
