$root = git rev-parse --show-toplevel 2>$null
if ($LASTEXITCODE -ne 0) { exit 0 }
$cwd = (Get-Location).Path

$cwdNormalized = $cwd.Replace('\', '/').TrimEnd('/')
$rootNormalized = $root.Replace('\', '/').TrimEnd('/')

if ($cwdNormalized -ne $rootNormalized) {
    $msg = "[MANDATORY] CWD=$cwdNormalized != $rootNormalized. Run: cd $rootNormalized"
    $json = @{
        hookSpecificOutput = @{
            hookEventName = "PostToolUse"
            additionalContext = $msg
        }
    } | ConvertTo-Json -Compress
    Write-Output $json
}
