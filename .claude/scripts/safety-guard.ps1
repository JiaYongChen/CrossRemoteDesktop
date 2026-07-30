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

# ── 特征提取 ─────────────────────────────────────────
# 取第一条命令（跳过 && || ; 连接符和管道 |）
$firstCmd = ($command -split '[;&|]' | Select-Object -First 1).Trim()
if (-not $firstCmd) { return }

$tokens = $firstCmd -split '\s+'
$mainCmd = ($tokens[0] -replace '^.*[/\\]', '').ToLower()  # 剥离路径前缀
# ── 第一层：静态白名单 ───────────────────────────────
$lowRiskPatterns = @(
    # 读操作
    '^get-content$', '^get-childitem$', '^ls$', '^dir$', '^cat$',
    '^select-string$', '^test-path$', '^where(\.exe)?$', '^measure-object$',
    '^select-object$', '^format-', '^write-output$', '^echo$', '^type$',
    '^findstr$', '^tasklist$',
    # 构建/测试
    '^cmake$', '^ctest$', '^msbuild$',
    # 文件创建
    '^new-item$', '^out-file$', '^set-content$', '^mkdir$',
    # 环境变量
    '^set$', '^export$', '^printenv$',
    # Git 只读
    '^git$'
)

$isLowRisk = $false
$reason = ""

foreach ($pattern in $lowRiskPatterns) {
    if ($mainCmd -match $pattern) {
        $isLowRisk = $true
        $reason = "静态规则: $mainCmd"
        break
    }
}

# Git 命令细分：只读放行，其他不判定
if ($mainCmd -eq 'git' -and $tokens.Count -ge 2) {
    $gitSub = $tokens[1].ToLower()
    $gitReadOnly = @('status', 'log', 'diff', 'show', 'rev-parse', 'remote', 'stash', 'branch', 'tag', 'config', 'ls-files', 'ls-tree', 'describe', 'blame', 'grep', 'rev-list', 'whatchanged', 'shortlog', 'reflog', 'cherry', 'format-patch', 'archive', 'bundle', 'verify-commit', 'verify-tag', 'check-ref-format', 'check-ignore', 'check-attr', 'check-mailmap', 'column', 'credential', 'difftool', 'fetch', 'help', 'instaweb', 'mergetool', 'notes', 'range-diff', 'replace', 'request-pull', 'show-branch')
    if ($gitSub -in $gitReadOnly) {
        $isLowRisk = $true
        $reason = "静态规则: git $gitSub (只读)"
    } else {
        $isLowRisk = $false
    }
}

# 环境变量赋值模式
if ($firstCmd -match '^\$\w+\s*=' -or $firstCmd -match '^[A-Z_]+\s*=') {
    $isLowRisk = $true
    $reason = "静态规则: 环境变量赋值"
}

# 危险操作直接跳过静态规则（不进白名单）
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
foreach ($pattern in $dangerousPatterns) {
    if ($firstCmd -match $pattern) {
        $isLowRisk = $false
        break
    }
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
# 凭据解析：优先 ANTHROPIC_API_KEY（官方 x-api-key），其次 ANTHROPIC_AUTH_TOKEN（代理 Bearer）
$apiKey = $env:ANTHROPIC_API_KEY
$authToken = $env:ANTHROPIC_AUTH_TOKEN
if (-not $apiKey -and -not $authToken) {
    # 无可用凭据，不做决策
    Write-Output (ConvertTo-Json -Compress -Depth 2 @{
        hookSpecificOutput = @{
            hookEventName = "PreToolUse"
        }
    })
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

try {
    $response = Invoke-RestMethod `
        -Uri $apiUri `
        -Method Post `
        -Headers $headers `
        -Body ([System.Text.Encoding]::UTF8.GetBytes($body)) `
        -TimeoutSec 10

    # 思考模型（如 qwen3.8-max-preview）的可见答复在 type=text 块，content[0] 可能是 thinking 块
    $textBlock = $response.content | Where-Object { $_.type -eq 'text' } | Select-Object -Last 1
    $aiText = if ($textBlock) { $textBlock.text.Trim() } else { "" }
    # 提取 JSON（AI 可能包裹在 markdown 代码块中）
    if ($aiText -match '\{[\s\S]*\}') {
        $aiJson = $Matches[0]
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
        }
    }
} catch {
    # 超时或请求失败，不做决策
}

# 不做决策，走正常弹窗
Write-Output (ConvertTo-Json -Compress -Depth 2 @{
    hookSpecificOutput = @{
        hookEventName = "PreToolUse"
    }
})
